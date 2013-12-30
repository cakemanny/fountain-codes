Fountain Codes
==============

Origin
------

This project arose out of a challenge presented on http://programmingpraxis.com
in which it was required to create a program that sent and received and
decoded data in
[fountain codes](http://programmingpraxis.com/2012/09/04/fountain-codes/) 

Since last updating this text, the following have been completed

* Working version on unix based systems (as much as you consider it 'working')
* Client and server network pair
* Added license

To be resolved:

* No checksumming on receipt
* NULL's up to the length of a blocksize are appended to the end of the output
* Misalignement of blocks when serving from windows
* segfault when 32-bit linux serves to 64-bit mac (program rather than OS)


