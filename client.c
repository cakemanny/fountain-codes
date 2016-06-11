#define _GNU_SOURCE // asks stdio.h to include asprintf

/* Load in OS specific networking code */
#include "networking.h"

#include <stdio.h>
#include <stdlib.h> //memcpy
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h> //getopt
#include <getopt.h> //getopt_long
#include <assert.h>

#ifdef _WIN32
#   include <fcntl.h> // open -- the mingw unix open
#   include <sys/stat.h> //  permissions for _wopen
#   include "asprintf.h"
#   include "stpcpy.h"
#endif
#include "errors.h"
#include "fountain.h"
#include "dbg.h"
#include "fountainprotocol.h" // msg definitions
#include "mapping.h" // map_file unmap_file

#define DEFAULT_PORT 2534
#define DEFAULT_IP "127.0.0.1"
#define BURST_SIZE 1000
#define NUM_CACHES 4

// ------ types ------

typedef struct ftn_cache_s {
    int capacity;
    int size;
    // should probably also have file ref in here
    int section;
    fountain_s** base;
    fountain_s** current;
    struct ftn_cache_s* next;
} ftn_cache_s;

typedef struct stats_s {
    int num_requested;
    int num_recvd;
    int num_discarded;
} stats_s;

// ------ Forward declarations ------
static int proc_file(file_info_s* file_info);
static int create_connection();
static void close_connection();
static fountain_s* get_ftn_from_network(int section, int num_sections);
static int get_remote_file_info(struct file_info_s*);
static void platform_truncate(const char* filename, int length);
static char* sanitize_path(const char* unsafepath) __malloc;
static int file_info_bytes_per_section(file_info_s* info);
static int file_info_calc_num_sections(file_info_s* info);

struct option long_options[] = {
    { "cachemul",   required_argument,  NULL, 'c' },
    { "help",       no_argument,        NULL, 'h' },
    { "ip",         required_argument,  NULL, 'i' },
    { "output",     required_argument,  NULL, 'o' },
    { "port",       required_argument,  NULL, 'p' },
    { 0, 0, 0, 0 }
};

// ------ static variables ------
static SOCKET s = INVALID_SOCKET;
static int port = DEFAULT_PORT;
static char* remote_addr = DEFAULT_IP;
static char const * program_name = NULL;

static char * outfilename = NULL;

// The buffer for our pulling packets off the network
static char* netbuf = NULL;
static int netbuf_len;

static int section_size_in_blocks = -1;
static int cache_size_multiplier = 6;

static stats_s stats = { };

// ------ functions ------
static void print_usage_and_exit(int status) {
    FILE* out = (status == 0) ? stdout : stderr;

    fprintf(out, "Usage: %s [OPTION]...\n", program_name);
    fputs("\
\n\
  -h, --help                display this help message\n\
  -c, --cachemul=N          cache size as multiple of section size\n\
  -i, --ip=IPADDRESS        ip address of the remote host\n\
  -o, --output=FILENAME     output file name\n\
  -p, --port=PORT           port to connect to\n\
", out);
    exit(status);
}

int main(int argc, char** argv) {
    /* deal with options */
    program_name = argv[0];
    int c;
    while ( (c = getopt_long(argc, argv, "c:hi:o:p:", long_options, NULL)) != -1 ) {
        switch (c) {
            case 'c':
                cache_size_multiplier = atoi(optarg);
                break;
            case 'h':
                print_usage_and_exit(0);
                break;
            case 'i':
                remote_addr = optarg;
                break;
            case 'o':
                outfilename = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case '?':
                print_usage_and_exit(1);
                break;
            default: // Shouldn't happen
                fprintf(stderr, "bad option: %c\n", optopt);
                print_usage_and_exit(1);
                break;
        }
    }

    int error;
    if ( (error = create_connection()) < 0 ) {
        close_connection();
        return handle_error(ERR_CONNECTION, NULL);
    }

    int recvbuf_size = 0;
    socklen_t recvbuf_size_size = 0;
    if (getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&recvbuf_size, &recvbuf_size_size) == 0) {
        debug("Current recv buffer size is %d bytes", recvbuf_size);
    }

    struct sockaddr_in server_address = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr.s_addr = inet_addr(remote_addr)
    };

    // define this above any jumps
    int i_should_free_outfilename = 0;

    // Let's "connect" our UDP socket to the remote address to
    // simplify the code below
    if (connect(s, (struct sockaddr*)&server_address, sizeof server_address) < 0) {
        log_err("Failed to connect UDP socket - wtf!");
        goto shutdown;
    }

    struct file_info_s file_info;
    if (get_remote_file_info(&file_info) < 0) {
        log_err("Failed to get information about the remote file");
        goto shutdown;
    }
    debug("Downloading %s", file_info.filename);
    odebug("%d", file_info.section_size);
    odebug("%d", file_info.blk_size);
    if (file_info.blk_size > MAX_BLOCK_SIZE) {
        log_err("Block size (%"PRId16") larger than allowed: %d",
                  file_info.blk_size, MAX_BLOCK_SIZE);
    }

    int to_alloc = 512;
    while (to_alloc < file_info.blk_size + FTN_HEADER_SIZE + sizeof(uint16_t)) {
        to_alloc <<= 1;
    }
    netbuf = malloc(to_alloc);
    if (!netbuf) {
        log_err("Failed to allocate the network buffer");
        goto shutdown;
    }
    netbuf_len = to_alloc;

    if (!outfilename) {
        outfilename = sanitize_path(file_info.filename);
        if (!outfilename)
            goto shutdown;
        debug("Sanitized path: %s", outfilename);
        i_should_free_outfilename = 1;
    }

    // We will at some point need to truncate to a whole number of blocks
    // when we introduce memory mapped files into the equation
    int num_sections = file_info_calc_num_sections(&file_info);
    int bytes_per_section = file_info_bytes_per_section(&file_info);
    platform_truncate(outfilename, num_sections * bytes_per_section);

    section_size_in_blocks = file_info.section_size;
    // do { get some packets, try to decode } while ( not decoded )
    if (proc_file(&file_info) < 0)
        goto shutdown;

    platform_truncate(outfilename, file_info.filesize);

    odebug("%d", stats.num_requested);
    odebug("%d", stats.num_recvd);
    odebug("%d", stats.num_discarded);

shutdown:
    if (i_should_free_outfilename)
        free(outfilename);
    if (netbuf)
        free(netbuf);
    close_connection();
}

void platform_truncate(const char* filename, int length) {
    #ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
        wchar_t wfilename[wlen];
        MultiByteToWideChar(CP_UTF8, 0, filename, -1, wfilename, wlen);

        int fd = _wopen(wfilename, _O_CREAT | _O_RDWR | _O_APPEND, _S_IREAD | _S_IWRITE);
        if (fd >= 0 && ftruncate(fd, length) >= 0) {
            _close(fd);
        } else
            log_err("Failed to truncate the output file");
    #else
        fclose(fopen(filename, "ab+"));
        if (truncate(filename, length) < 0)
            log_err("Failed to truncate the output file");
    #endif // _WIN32
}

char* sanitize_path(const char* unsafepath)
{
#ifdef _WIN32
    /*
     * If we are on windows then we need to remove or check for path components
     * which are not allowed in filenames: \/:*?<>|
     */
    /* Note that we consider \ to be illegal because it would have been
     * converted into a / if server is running on windows
     */
    char illegal_chars[] = "\\:*?<>|";
    if (strpbrk(unsafepath, illegal_chars) != NULL) {
        log_err("Illegal characters in path: %s", unsafepath);
        return NULL;
    }
#endif // _WIN32

    char* safepath = NULL;
    /*
     * We have to jail the path into the current folder to avoid a MITM from
     * accessing the reset of the system
     */
    char* path = strdup(unsafepath);
    check_mem(path);
    char* sep = "/";

    char* seg0[256] = {0};
    char** cur_seg = seg0;
    char** seg_end = seg0 + 256;

    char* rest = NULL;
    for (char* segment = strtok_r(path, sep, &rest);
         segment;
         segment = strtok_r(NULL, sep, &rest))
    {
        if (cur_seg == seg_end) {
            log_err("too many path segments");
            goto error;
        }
        if (segment[0] == '.') {
            if (segment[1] == '\0') { /* current dir => skip */
                continue;
            }
            if (segment[1] == '.' && segment[2] == '\0') {
                /* parent dir => level up if not at root */
                if (cur_seg != seg0)
                    --cur_seg;
                continue;
            }
        }
        *cur_seg++ = segment; /* otherwise include in path array */
    }

    /* the output path won't be longer that the input */
    safepath = calloc(1 + strlen(unsafepath), sizeof *safepath);
    check_mem(safepath);
    char* sp = safepath;
    for (char** p = seg0; p < cur_seg - 1; p++) {
        /* don't need to use stpncopy because sum(len(seg0)) is shorter than
           safepath by construction */
        sp = stpcpy(sp, *p);
        *sp++ = '/';
    }
    sp = stpcpy(sp, *(cur_seg - 1));

error:
    if (path) free(path);

    return safepath;
}

int create_connection() {

    #ifdef _WIN32
    WSADATA w;
    if (WSAStartup(0x0202, &w)) return -10;
    if (w.wVersion != 0x0202)   return -20;
    #endif

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)    return -30;

    return 0;
}

void close_connection() {
    if (s)
        closesocket(s);
    #ifdef _WIN32
    WSACleanup();
    #endif
}

static void packet_order_for_network(packet_s* packet) {
    fp_to(packet->magic);
}
//static void packet_order_from_network(packet_s* packet) {
//    packet->magic = ntohl(packet->magic);
//}

static void file_info_order_from_network(file_info_s* info) {
    fp_from(info->magic);
    fp_from(info->section_size);
    fp_from(info->blk_size);
    fp_from(info->filesize);
}

static void wait_signal_order_for_network(wait_signal_s* wait_signal) {
    int n = wait_signal->num_sections;
    fp_to(wait_signal->magic);
    fp_to(wait_signal->num_sections);
    for (int i = 0; i < n; i++) {
        fp_to(wait_signal->sections[i].section);
        fp_to(wait_signal->sections[i].capacity);
    }
}


static int send_wait_signal(int num_sections, int* sections, int* capacities) {
    for (int i = 0; i < num_sections; i++)
        stats.num_requested += capacities[i];
    int packet_size = sizeof(wait_signal_s) + num_sections * 2 * sizeof(uint16_t);
    wait_signal_s* msg = calloc(1, packet_size);
    check_mem(msg);

    msg->magic = MAGIC_WAITING;
    msg->num_sections = (uint16_t)num_sections;
    for (int i = 0; i < num_sections; i++) {
        msg->sections[i].section = sections[i];
        msg->sections[i].capacity = capacities[i];
    }

    for (int i = 0; i < num_sections; i++) {
        debug("Sending wait signal with capacity = %d", capacities[i]);
    }
    wait_signal_order_for_network(msg);
    int result = send(s, (void*)msg, packet_size, 0);
    free(msg);
    return (result < 0) ? result : 0;
error:
    return ERR_MEM;
}

static int recv_msg(char* buf, size_t buf_len) {
    memset(buf, '\0', buf_len);
    int bytes_recvd = recv(s, buf, buf_len, 0);
    if (bytes_recvd < 0) {
        log_err("Error reading from network");
        return ERR_NETWORK;
    }
    debug("Received %d bytes", bytes_recvd);

    return bytes_recvd;
}

static int send_file_info_request() {
    info_request_s msg = {
        .magic = MAGIC_REQUEST_INFO,
    };
    packet_order_for_network((packet_s*)&msg);
    int result = send(s, (void*)&msg, sizeof msg, 0);
    return (result < 0) ? result : 0;
}

int get_remote_file_info(file_info_s* file_info) {
    int result = send_file_info_request();
    if (result < 0) return result;

    int bytes_recvd = recv_msg((char*)file_info, sizeof *file_info);
    if (bytes_recvd < 0) return -1;
    file_info_order_from_network(file_info);
    if (file_info->magic == MAGIC_INFO) {
        // TODO: define max & min acceptable blocksizes and sanity check
        // TODO: check
        // blk_size * (num_blocks - 1) <= filesize <= blk_size * num_blocks ?
        if (file_info->blk_size < 0
                || file_info->filesize < 0) {
            log_err("Corrupt packet");
            return ERR_NETWORK;
        }
        return 0;
    }
    log_err("Packet was not a fileinfo packet");
    return -1;
}

static ftn_cache_s* ftn_cache_alloc() __malloc;
ftn_cache_s* ftn_cache_alloc() {
    assert(section_size_in_blocks > 0);

    ftn_cache_s* cache = malloc(sizeof *cache);
    check_mem(cache);
    memset(cache, 0, sizeof *cache);

    cache->capacity = cache_size_multiplier * section_size_in_blocks;
    cache->section = -1;

    cache->base = malloc(cache->capacity * sizeof *cache->base);
    check_mem(cache->base);
    cache->current = cache->base;

    return cache;
error:
    if (cache)
        free(cache);
    return NULL;
}

static void handle_pollevents(struct pollfd* pfd) {
    if (pfd->revents & POLLERR)
        log_err("POLLERR: An error has occurred");
    if (pfd->revents & POLLHUP)
        log_err("POLLHUP: A stream-oriented connection was either "
                "disconnected or aborted.");
    if (pfd->revents & POLLNVAL)
        log_err("POLLNVAL: Invalid socket");
}


typedef struct { int sections[NUM_CACHES]; int caps[NUM_CACHES]; } capacities_s;

static capacities_s get_capacities(ftn_cache_s* cache, int num_sections) {
    capacities_s result = { };
    ftn_cache_s* c = cache;
    for (int i = 0; i < num_sections; i++) {
        result.sections[i] = c->section;
        result.caps[i] = c->capacity - c->size;
        c = c->next;
    }
    return result;
}

static void load_from_network(ftn_cache_s* cache, int num_sections) {
    assert( num_sections <= NUM_CACHES ); // We can maybe increase this at some point

    struct pollfd pfd = {
        .fd = s,
        .events = POLLIN,
        .revents = 0
    };

    int pollret1 = poll(&pfd, 1, 0);
    if (pollret1 < 0) {
        log_err("Error when waiting to receive packets");
        return;
    }
    if (pollret1 > 0 && !(POLLIN & pfd.revents)) {
        handle_pollevents(&pfd);
        return;
    }
    if (pollret1 == 0) {
        capacities_s c = get_capacities(cache, num_sections);
        // FIXME: check return code
        send_wait_signal(num_sections, c.sections, c.caps);
    }

    static const int max_timeout = 15000;
    int timeout = 10;

    int total_capacities = ({
        int sum = 0;
        capacities_s c = get_capacities(cache, num_sections);
        for (int i = 0; i < num_sections; i++) { sum += c.caps[i]; }
        sum;
    });
    for (int i = 0; i < total_capacities; i++) {
        int pollret = poll(&pfd, 1, timeout);
        if (pollret == 0) {
            if (cache->size > 0) {
                debug("Waited too long - time to decode instead");
                break;
            } else {
                if (timeout / 2 >= max_timeout) {
                    log_err("Timed out after %.00lf seconds",
                            (double)max_timeout / 1000.0);
                    return;
                }
                capacities_s c = get_capacities(cache, num_sections);
                // FIXME: check return code
                send_wait_signal(num_sections, c.sections, c.caps);
                timeout <<= 1;
                continue;
            }
        } else if (pollret < 0) {
            log_err("Error when waiting for network activity");
            cache->size = 0;
            return;
        } else if (!(POLLIN & pfd.revents)) {
            log_err("Some networky problem...");
            handle_pollevents(&pfd);
            cache->size = 0;
            return;
        }

        int bytes_recvd = recv_msg(netbuf, netbuf_len);
        if (bytes_recvd < 0) {
            handle_error(bytes_recvd, NULL);
            return;
        }
        stats.num_recvd += 1;

        buffer_s packet = {
            .length = bytes_recvd,
            .buffer = netbuf
        };
        fountain_s* ftn = unpack_fountain(packet, section_size_in_blocks);
        if (ftn == NULL) { // Checksum may have failed
            // If the system runs out of memory this may become an infinite
            // loop... we could create an int offset instead of using
            // i, but that may end the program if we get too many bad packets.
            // Both are unlikely. May have to consider using some sort of
            // error code instead
            continue;
        }
        // find cache for this section
        ftn_cache_s* c = cache;
        while (c != NULL) {
            if (ftn->section == c->section) {
                c->base[c->size++] = ftn;
                break;
            }
            c = c->next;
        }
        if (c == NULL) {
            // Must be an old packet from a previous request
            debug("discarding fountain from section %d", ftn->section);
            free_fountain(ftn);
            continue;
        }
    }
    ftn_cache_s* c = cache;
    for (int i = 0; i < num_sections; i++) {
        c->current = c->base;
        debug("Cache size is now %d", c->size);
        c = c->next;
    }
}

fountain_s* get_ftn_from_network(int section, int num_sections) {
    static ftn_cache_s* cache = NULL;
    if (cache == NULL) {
        // Allocate
        ftn_cache_s** p = &cache;
        for (int i = 0; i < NUM_CACHES; i++) {
            *p = ftn_cache_alloc();
            if (!*p)
                return NULL; // FIXME: we can do better
            p = &(*p)->next; // p points to the `next` pointer of what p pointed to
        }
    }

    if (cache->section != section) {
        debug("Throwing away %d packets", cache->size);
        stats.num_discarded += cache->size;
        while (cache->size > 0) {
            fountain_s* to_delete = *cache->current;
            *cache->current++ = NULL;
            --cache->size;
            free_fountain(to_delete);
        }
        assert(cache->size == 0);
        ftn_cache_s* c = cache;
        cache = cache->next;
        cache->section = section; // if cache-section == -1
        c->section = -1;
        ftn_cache_s** p = &cache;
        while (*p != NULL) p = &(*p)->next; // p ends up pointing the null next pointer
        *p = c;
        c->next = NULL;
    }

    if (cache->size == 0) {
        debug("Cache size 0 - loading §%d from network...", section);
        int n_to_req = (num_sections - section > NUM_CACHES)
                        ? NUM_CACHES : num_sections - section;
        assert( n_to_req > 0 && n_to_req <= NUM_CACHES );
        odebug("%d", n_to_req);
        ftn_cache_s* c = cache;
        for (int i = 0; i < n_to_req; i++) {
            c->section = section + i;
            c = c->next;
        };
        load_from_network(cache, n_to_req);

        if (cache->size == 0) return NULL;
    }

    fountain_s* output = *cache->current;
    *cache->current++ = NULL;
    --cache->size;
    return output;
}

int file_info_bytes_per_section(file_info_s* info)
{
    return info->blk_size * info->section_size;
}
int file_info_calc_num_sections(file_info_s* info)
{
    int bytes_per_section = file_info_bytes_per_section(info);
    return (info->filesize + bytes_per_section - 1) / bytes_per_section;
}

/* process fountains as they come down the wire
   returns a status code (see errors.c)
 */
int proc_file(file_info_s* file_info) {
    int result = 0;
    char * err_str = NULL;

    char* file_mapping = map_file(outfilename);
    if (!file_mapping) {
        return handle_error(ERR_MAP, outfilename);
    }

    uint64_t total_packets = 0;

    int num_sections = file_info_calc_num_sections(file_info);
    odebug("%d", num_sections);
    int bytes_per_section = file_info_bytes_per_section(file_info);
    odebug("%d", bytes_per_section);
    for (int section_num = 0; section_num < num_sections; section_num++) {
        decodestate_s* state =
            decodestate_new(file_info->blk_size, file_info->section_size);
        if (!state) return handle_error(ERR_MEM, NULL);

        // Have to declare this above the first goto cleanup
        fountain_s* ftn = NULL;

        decodestate_s* tmp_ptr;
        tmp_ptr = realloc(state, sizeof(memdecodestate_s));
        if (tmp_ptr) state = tmp_ptr;
        else { result = ERR_MEM; goto cleanup; }

        state->filename = memdecodestate_filename;

        ((memdecodestate_s*)state)->result = file_mapping + (section_num * bytes_per_section);

        do {
            ftn = get_ftn_from_network(section_num, num_sections);
            if (!ftn) goto cleanup;
            state->packets_so_far++;
            result = memdecode_fountain((memdecodestate_s*)state, ftn);
            free_fountain(ftn);
            if (result < 0) goto cleanup;
        } while (!decodestate_is_decoded(state));

        log_info("Packets required for section %d: %d", section_num, state->packets_so_far);
        total_packets += state->packets_so_far;
cleanup:
        if (state)
            decodestate_free(state);
        if (result < 0 || !ftn)
            break;
    }
    log_info("Total packets required for download: %"PRIu64, total_packets);

    if (file_mapping) unmap_file(file_mapping);
    return handle_error(result, err_str);
}

