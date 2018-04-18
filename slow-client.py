#!/usr/bin/python

# slow-client.py - This is a client that makes an HTTP request very slowly, so
#                  it forces the proxy to read the request across multiple reads.
#
# usage: slow-client.py <proxy_url> <origin_url> <sleeptime> <timeout> <output>
#
import signal
import socket
import sys
import time
import urlparse

def handle_alarm(sig, frame):
    sys.exit(1)

def main():
    signal.signal(signal.SIGALRM, handle_alarm)

    (scheme, netloc, path, params, query, fragment) = urlparse.urlparse(sys.argv[1])
    proxyhost, proxyport = netloc.split(':')
    proxyport = int(proxyport)

    (scheme, netloc, path, params, query, fragment) = urlparse.urlparse(sys.argv[2])
    try:
        server, port = netloc.split(':')
        port = int(port)
    except ValueError as ve:
        server = netloc
        port = 80
    sleep_time = int(sys.argv[3])
    timeout = int(sys.argv[4])

    with open(sys.argv[5], 'wb') as fh:
        #create an INET, STREAMing socket
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((proxyhost, proxyport))
        time.sleep(sleep_time)
        s.send('GET %s HTTP/1.0\r\n' % (sys.argv[2]))
        time.sleep(sleep_time)
        s.send('Host: %s:%d\r\n\r\n' % (server, port))

        # set an alarm, so we can exit if it times out
        signal.alarm(timeout)

        content = b''
        while True:
            buf = s.recv(1024)
            if not buf:
                break
            content += buf
        # set an alarm, so we can exit if it times out
        signal.signal(signal.SIGALRM, signal.SIG_IGN)

        start_of_headers = content.index('\r\n\r\n')

        if start_of_headers >= 0:
            fh.write(content[start_of_headers+4:])

if __name__ == '__main__':
    main()
