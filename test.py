#!/usr/bin/python3

import socket

HOST = '127.0.0.1'
PORT = 2534

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(bytes('FCWAITING\r\n','UTF-8'), (HOST,PORT))
    try:
        (data, address) = s.recvfrom(1024)
        print("from address:", address)
        print("data:", data)
    except ConnectionResetError as e:
        print('The server is not currently listening / taking connections:')
        print( e)
    s.close()

if __name__ == '__main__':
    main()

