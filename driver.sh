#!/bin/bash
#
# driver.sh - This is a simple autograder for the Proxy Lab. It does
#     basic sanity checks that determine whether or not the code
#     behaves like a concurrent caching proxy. 
#
#     David O'Hallaron, Carnegie Mellon University
#     updated: 2/8/2016
# 
#     usage: ./driver.sh
# 

#TODO:
#   - Check Readers/Writers
#   - Check cache eviction
#   - Check maximum cache object size


# Point values - I/O multiplexing
MAX_BASIC=20
MAX_CONCURRENCY=50
MAX_LOG=10
MAX_CLEANUP=6
MAX_CACHE=10

# Various constants
HOME_DIR=`pwd`
PROXY_DIR="./.proxy"
NOPROXY_DIR="./.noproxy"
VALGRIND_LOG=".valgrind.log"
BASIC_TEST_TIME_FILE=".basicTestStart"
TIMEOUT=10
MAX_RAND=63000
PORT_START=1024
PORT_MAX=65000
MAX_PORT_TRIES=10

# List of text and binary files for the basic test
BASIC_LIST="home.html
            csapp.c
            tiny.c
            godzilla.jpg
            cgi-bin/slow?1&4096
            tiny"

# List of text files for the cache test
CACHE_LIST="tiny.c
            home.html
            csapp.c"

# The file we will fetch for various tests
FETCH_FILE="home.html"
FETCH_FILE_SLOW="cgi-bin/slow?1&4096"

NUM_PROCESSES_EXPECTED=$1
NUM_THREADS_EXPECTED=$2

#####
# Helper functions
#

#

#
# filesystem_friendly - escape &, ?, and / characters which might interfere with the shell
# usage: filesystem_friendly <path>
#
function filesystem_friendly {
	echo $1 | tr '/?&' '---'
}

#
# download_proxy - download a file from the origin server via the proxy
# usage: download_proxy <testdir> <filename> <origin_url> <proxy_url>
#
function download_proxy {
    cd $1
    curl --max-time ${TIMEOUT} --silent --proxy $4 --output $2 $3
    (( $? == 28 )) && echo "Error: Fetch timed out after ${TIMEOUT} seconds"
    cd $HOME_DIR
}

#
# download_proxy - download a file from the origin server via the proxy
# usage: download_proxy <testdir> <filename> <origin_url> <proxy_url>
#
function download_proxy_slow {
    cd $1
    $HOME_DIR/slow-client.py $4 $3 1 ${TIMEOUT} $2
    (( $? == 1 )) && echo "Error: Fetch timed out after ${TIMEOUT} seconds"
    cd $HOME_DIR
}

#
# download_noproxy - download a file directly from the origin server
# usage: download_noproxy <testdir> <filename> <origin_url>
#
function download_noproxy {
    cd $1
    curl --max-time ${TIMEOUT} --silent --output $2 $3 
    (( $? == 28 )) && echo "Error: Fetch timed out after ${TIMEOUT} seconds"
    cd $HOME_DIR
}

#
# clear_dirs - Clear the download directories
#
function clear_dirs {
    rm -rf ${PROXY_DIR}/*
    rm -rf ${NOPROXY_DIR}/*
}

#
# wait_for_port_use - Spins until the TCP port number passed as an
#     argument is actually being used. Times out after 5 seconds.
#
function wait_for_port_use() {
    timeout_count="0"
    portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
        | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
        | grep -E "[0-9]+" | uniq | tr "\n" " "`

    echo "${portsinuse}" | grep -wq "${1}"
    while [ "$?" != "0" ]
    do
        timeout_count=`expr ${timeout_count} + 1`
        if [ "${timeout_count}" == "${MAX_PORT_TRIES}" ]; then
            kill -ALRM $$
        fi

        sleep 1
        portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
            | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
            | grep -E "[0-9]+" | uniq | tr "\n" " "`
        echo "${portsinuse}" | grep -wq "${1}"
    done
}


#
# free_port - returns an available unused TCP port 
#
function free_port {
    # Generate a random port in the range [PORT_START,
    # PORT_START+MAX_RAND]. This is needed to avoid collisions when many
    # students are running the driver on the same machine.
    port=$((( RANDOM % ${MAX_RAND}) + ${PORT_START}))

    while [ TRUE ] 
    do
        portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
            | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
            | grep -E "[0-9]+" | uniq | tr "\n" " "`

        echo "${portsinuse}" | grep -wq "${port}"
        if [ "$?" == "0" ]; then
            if [ $port -eq ${PORT_MAX} ]
            then
                echo "-1"
                return
            fi
            port=`expr ${port} + 1`
        else
            echo "${port}"
            return
        fi
    done
}


#######
# Main 
#######

######
# Verify that we have all of the expected files with the right
# permissions
#

# Kill any stray proxies or tiny servers owned by this user
killall -q proxy tiny nop-server.py 2> /dev/null

# Make sure we have a Tiny directory
if [ ! -d ./tiny ]
then 
    echo "Error: ./tiny directory not found."
    exit
fi

# If there is no Tiny executable, then try to build it
if [ ! -x ./tiny/tiny ]
then 
    echo "Building the tiny executable."
    (cd ./tiny; make)
    echo ""
fi

# Make sure we have all the Tiny files we need
if [ ! -x ./tiny/tiny ]
then 
    echo "Error: ./tiny/tiny not found or not an executable file."
    exit
fi
for file in ${BASIC_LIST}
do
    file=`echo ${file} | sed s/\?.*//`
    if [ ! -e ./tiny/${file} ]
    then
        echo "Error: ./tiny/${file} not found."
        exit
    fi
done

# Make sure we have an existing executable proxy
if [ ! -x ./proxy ]
then 
    echo "Error: ./proxy not found or not an executable file. Please rebuild your proxy and try again."
    exit
fi

# Make sure we have an existing executable nop-server.py file
if [ ! -x ./nop-server.py ]
then 
    echo "Error: ./nop-server.py not found or not an executable file."
    exit
fi

# Create the test directories if needed
if [ ! -d ${PROXY_DIR} ]
then
    mkdir ${PROXY_DIR}
fi

if [ ! -d ${NOPROXY_DIR} ]
then
    mkdir ${NOPROXY_DIR}
fi

# Add a handler to generate a meaningful timeout message
trap 'echo "Timeout waiting for the server to grab the port reserved for it"; kill $$' ALRM

#####
# Basic
#
echo "*** Basic ***"

# create a file then, sleep, so this file will be older than the log file
# created by the proxy
touch $BASIC_TEST_TIME_FILE
sleep 1
rm -f $VALGRIND_LOG

# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on ${tiny_port}"
cd ./tiny
./tiny ${tiny_port}   &> /dev/null  &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on ${proxy_port}"
valgrind --log-file=$VALGRIND_LOG --leak-check=full ./proxy ${proxy_port}  &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"

num_threads_pre=`ps --no-headers -Lo lwp --pid $proxy_pid | sort -u | wc -l`
num_processes_pre=`ps --no-headers -o pid --pid $proxy_pid --ppid $proxy_pid | sort -u | wc -l`

# Now do the test by fetching some text and binary files directly from
# Tiny and via the proxy, and then comparing the results.
numRun=0
numSucceeded=0
for file in ${BASIC_LIST}
do
    numRun=`expr $numRun + 1`
    echo "${numRun}: ${file}"
    clear_dirs

    file_local=`filesystem_friendly $file`

    # Fetch using the proxy
    echo "   Fetching ./tiny/${file} into ${PROXY_DIR} using the proxy"
    download_proxy $PROXY_DIR ${file_local} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"

    # Fetch directly from Tiny
    echo "   Fetching ./tiny/${file} into ${NOPROXY_DIR} directly from Tiny"
    download_noproxy $NOPROXY_DIR ${file_local} "http://localhost:${tiny_port}/${file}"

    # Compare the two files
    echo "   Comparing the two files"
    diff -q ${PROXY_DIR}/${file_local} ${NOPROXY_DIR}/${file_local} &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
        echo "   Success: Files are identical."
    else
        echo "   Failure: Files differ."
    fi
done

echo "Killing tiny and proxy"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null
kill -INT $proxy_pid 2> /dev/null
sleep 2
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

basicScore=`expr ${MAX_BASIC} \* ${numSucceeded} / ${numRun}`

echo "basicScore: $basicScore/${MAX_BASIC}"


######
# Logging
#

echo ""
echo "*** Logging ***"

# look for the log file by identifying the most recent file in the current
# directory
#XXX - this is a hack
logfile=`find . -path $NOPROXY_DIR -prune -o -path $PROXY_DIR -prune -o \! -name $VALGRIND_LOG -type f -newer $BASIC_TEST_TIME_FILE -print | tail -1`
numRun=0
numSucceeded=0
if [ -z "$logfile" ]; then
    echo "   Could not identify log file"
elif ! [ -f "$logfile" ]; then
    echo "   log file $logfile not found"
else
    for file in ${BASIC_LIST}; do
        echo "   Checking log for http://localhost:${tiny_port}/${file}"
        numRun=`expr $numRun + 1`
        if grep -q "http://localhost:${tiny_port}/${file}" $logfile; then
            echo "   Success: Entry found"
            numSucceeded=`expr ${numSucceeded} + 1`
        else
            echo "   Failed: Entry not found"
        fi
    done
fi
rm -f $BASIC_TEST_TIME_FILE
rm -f $logfile

if (( $numRun == 0 )); then
    logScore=0
else
    logScore=`expr ${MAX_LOG} \* ${numSucceeded} / ${numRun}`
fi

echo "logScore: $logScore/${MAX_LOG}"

######
# Cleanup
#

echo ""
echo "*** Cleanup ***"

cleanupScore=${MAX_CLEANUP}
def_lost=`grep 'definitely lost:' $VALGRIND_LOG | awk '{ print $4 }'`
indir_lost=`grep 'indirectly lost:' $VALGRIND_LOG | awk '{ print $4 }'`
poss_lost=`grep 'possibly lost:' $VALGRIND_LOG | awk '{ print $4 }'`
still_reach=`grep 'still reachable:' $VALGRIND_LOG | awk '{ print $4 }'`

rm -f $VALGRIND_LOG

if [ \( -n "$def_lost" -a "$def_lost" != "0" \) -o \
	\( -n "$indir_lost" -a "$indir_lost" != "0" \) -o \
	\( -n "$poss_lost" -a "$poss_lost" != "0" \) ]; then
        echo "   Bytes lost, either definitely, indirectly, or possibly"
        cleanupScore=`expr ${cleanupScore} - 3`
fi

if [ -n "$still_reach" -a "$still_reach" != "0" ]; then
        echo "   Bytes still reachable"
        cleanupScore=`expr ${cleanupScore} - 3`
fi

echo "cleanupScore: $cleanupScore/${MAX_CLEANUP}"

######
# Concurrency
#

echo ""
echo "*** Concurrency ***"

# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on port ${tiny_port}"
cd ./tiny
./tiny ${tiny_port} &> /dev/null &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on port ${proxy_port}"
./proxy ${proxy_port} &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"

# Run a special blocking nop-server that never responds to requests
nop_port=$(free_port)
echo "Starting the blocking NOP server on port ${nop_port}"
./nop-server.py ${nop_port} &> /dev/null &
nop_pid=$!

# Wait for the nop server to start in earnest
wait_for_port_use "${nop_port}"

# Try to fetch a file from the blocking nop-server using the proxy
clear_dirs
echo "Trying to fetch a file from the blocking nop-server"
download_proxy $PROXY_DIR "nop-file.txt" "http://localhost:${nop_port}/nop-file.txt" "http://localhost:${proxy_port}" &

# Fetch directly from Tiny
echo "Fetching ./tiny/${FETCH_FILE_SLOW} into ${NOPROXY_DIR} directly from Tiny"
download_noproxy $NOPROXY_DIR "`filesystem_friendly ${FETCH_FILE_SLOW}`" "http://localhost:${tiny_port}/${FETCH_FILE_SLOW}"

# Fetch directly from Tiny
echo "Fetching ./tiny/${FETCH_FILE} into ${NOPROXY_DIR} directly from Tiny"
download_noproxy $NOPROXY_DIR "`filesystem_friendly ${FETCH_FILE}`" "http://localhost:${tiny_port}/${FETCH_FILE}"

bgjobs=""
# Fetch slow files using the proxy
echo "Fetching ./tiny/${FETCH_FILE_SLOW} into ${PROXY_DIR} using the proxy (10 times)"
for i in {1..10}; do
    download_proxy_slow $PROXY_DIR "`filesystem_friendly ${FETCH_FILE_SLOW}`-$i" "http://localhost:${tiny_port}/${FETCH_FILE_SLOW}&1" "http://localhost:${proxy_port}" &
    bgjobs="$! $bgjobs"
done
# Give the slow files a head start
sleep 1
# Fetch fast files using the proxy
echo "Fetching ./tiny/${FETCH_FILE} into ${PROXY_DIR} using the proxy (10 times)"
for i in {1..10}; do
    download_proxy $PROXY_DIR "`filesystem_friendly ${FETCH_FILE}`-$i" "http://localhost:${tiny_port}/${FETCH_FILE}?$i" "http://localhost:${proxy_port}" &
    bgjobs="$! $bgjobs"
done
# Give proxy a chance to connect to the server for each client
sleep 3
num_threads_realtime=`ps --no-headers -Lo lwp --pid $proxy_pid | sort -u | wc -l`
num_processes_realtime=`ps --no-headers -o pid --pid $proxy_pid | sort -u | wc -l`
fd_output=`lsof -p $proxy_pid +f g | awk '$4 ~ /^[0-9]/ { print $0 }' 2>/dev/null`
fd_output_proxy_port=$proxy_port
fd_output_tiny_port=$tiny_port

wait $bgjobs

numRun=0
numSucceeded=0
echo "Checking whether the concurrent proxy fetches succeeded"
# See if the proxy fetch succeeded
for i in {1..10}; do
    numRun=`expr $numRun + 1`
    diff -q "${PROXY_DIR}/`filesystem_friendly ${FETCH_FILE_SLOW}`-$i" "${NOPROXY_DIR}/`filesystem_friendly ${FETCH_FILE_SLOW}`" &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
    else
        echo "Failure: Was not able to fetch tiny/${FETCH_FILE_SLOW} from the proxy."
    fi
done
# See if the proxy fetch succeeded
for i in {1..10}; do
    numRun=`expr $numRun + 1`
    diff -q "${PROXY_DIR}/`filesystem_friendly ${FETCH_FILE}`-$i" "${NOPROXY_DIR}/`filesystem_friendly ${FETCH_FILE}`" &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
    else
        echo "Failure: Was not able to fetch tiny/${FETCH_FILE} from the proxy."
    fi
done
echo "Checking that the faster files were retrieved before the slower files"
# See if the fast files were retrieved first
for i in {1..10}; do
    numRun=`expr $numRun + 1`
    if [ "${PROXY_DIR}/`filesystem_friendly ${FETCH_FILE_SLOW}`-$i" -nt "${PROXY_DIR}/`filesystem_friendly ${FETCH_FILE}`-$i" ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
    else
        echo "Failure: tiny/${FETCH_FILE} older than tiny/${FETCH_FILE_SLOW}"
    fi
done

# Clean up
echo "Killing tiny, nop-server, and proxy"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null
kill $nop_pid 2> /dev/null
wait $nop_pid 2> /dev/null
# wait on proxy only after nop-server has finished, so we can allow it to
# "finish" any clients
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

concurrencyScore=`expr ${MAX_CONCURRENCY} \* ${numSucceeded} / ${numRun}`

echo "concurrencyScore: $concurrencyScore/${MAX_CONCURRENCY}"

#####
# Caching
#
echo ""
echo "*** Cache ***"

# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on port ${tiny_port}"
cd ./tiny
./tiny ${tiny_port} &> /dev/null &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on port ${proxy_port}"
./proxy ${proxy_port} &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"

# Fetch some files from tiny using the proxy
clear_dirs
for file in ${CACHE_LIST}
do
    echo "Fetching ./tiny/${file} into ${PROXY_DIR} using the proxy"
    download_proxy $PROXY_DIR ${file} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"
done

# Kill Tiny
echo "Killing tiny"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null

# Now try to fetch a cached copy of one of the fetched files.
echo "Fetching a cached copy of ./tiny/${FETCH_FILE} into ${NOPROXY_DIR}"
download_proxy $NOPROXY_DIR ${FETCH_FILE} "http://localhost:${tiny_port}/${FETCH_FILE}" "http://localhost:${proxy_port}"

# See if the proxy fetch succeeded by comparing it with the original
# file in the tiny directory
diff -q ./tiny/${FETCH_FILE} ${NOPROXY_DIR}/${FETCH_FILE}  &> /dev/null
if [ $? -eq 0 ]; then
    cacheScore=${MAX_CACHE}
    echo "Success: Was able to fetch tiny/${FETCH_FILE} from the cache."
else
    cacheScore=0
    echo "Failure: Was not able to fetch tiny/${FETCH_FILE} from the proxy cache."
fi

# Kill the proxy
echo "Killing proxy"
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

echo "cacheScore: $cacheScore/${MAX_CACHE}"

# Emit the total score
totalScore=`expr ${basicScore} + ${logScore} + ${cleanupScore} + ${cacheScore} + ${concurrencyScore}`
maxScore=`expr ${MAX_BASIC} + ${MAX_LOG} + ${MAX_CLEANUP} + ${MAX_CACHE} + ${MAX_CONCURRENCY}`
echo ""
echo "totalScore: ${totalScore}/${maxScore}"

#####
# Processes and Threads
#
echo ""
echo "*** Processes and Threads ***"
echo "Total processes (pre-load): $num_processes_pre"
echo "Total threads (pre-load): $num_threads_pre"
echo "Total processes (during load): $num_processes_realtime"
echo "Total threads (during load): $num_threads_realtime"

if [ $num_threads_pre -gt 1 ]; then
	echo "    A pool of $(( $num_threads_pre - 1 )) was created a program start."
else
	echo "    No thread pool was created at program start."
fi
if [ $num_threads_realtime -gt $num_threads_pre ]; then
	echo "    Threads are being spawned on the fly: $num_threads_realtime > $num_threads_pre"
else
	echo "    Threads are *not* being spawned on the fly"
fi
if [ $num_processes_realtime -gt $num_processes_pre ]; then
	echo "    Processes are being forked on the fly: $num_processes_realtime > $num_processes_pre"
else
	echo "    Processes are *not* being forked on the fly"
fi

#####
# Sockets and file descriptors
#
echo ""
echo "*** Sockets and file descriptors ***"

if echo -e "$fd_output" | awk '{ print $10 }' | grep -q eventpoll; then
	echo "    Active epoll file descriptor detected"
else
	echo "    No active epoll file descriptor detected"
fi
listenfd_flags=`echo -e "$fd_output" | awk '$9 == "TCP" && $10 ~ /:'$fd_output_proxy_port'$/ && $11 ~ /LISTEN/ { print $6 }'`
if [ -z "$listenfd_flags" ]; then
	echo "    Listening file descriptor could not be detected."
elif echo -e "$listenfd_flags" | grep -vq 'NB\|ND'; then
	echo "    Listening file descriptor is *not* configured as non-blocking"
else
	echo "    Listening file descriptor is configured as non-blocking"
fi
clientfd_flags=`echo -e "$fd_output" | awk '$9 == "TCP" && $10 ~/:'$fd_output_proxy_port'(->|$)/ { print $6 }'`
if [ -z "$clientfd_flags" ]; then
	echo "    Client file descriptors could not be detected."
elif echo -e "$clientfd_flags" | grep -vq 'NB\|ND'; then
	echo "    Client file descriptors are *not* configured as non-blocking"
else
	echo "    Client file descriptors are configured as non-blocking"
fi
serverfd_flags=`echo -e "$fd_output" | awk '$9 == "TCP" && $10 ~/:'$fd_output_tiny_port'(->|$)/ { print $6 }'`
if [ -z "$serverfd_flags" ]; then
	echo "    Server file descriptors could not be detected."
elif echo -e "$serverfd_flags" | grep -vq 'NB\|ND'; then
	echo "    Server file descriptors are *not* configured as non-blocking"
else
	echo "    Server file descriptors are configured as non-blocking"
fi

exit
