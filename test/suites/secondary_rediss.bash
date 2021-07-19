SUITE_secondary_rediss_PROBE() {
    if ! $CCACHE --version | fgrep -q -- redis-storage &> /dev/null; then
        echo "redis-storage not available"
        return
    fi
    if ! command -v redis-server &> /dev/null; then
        echo "redis-server not found"
        return
    fi
    if redis-server --tls-port 6379 --port 0 2>&1 | grep "FATAL CONFIG FILE ERROR" &> /dev/null; then
        # "Bad directive or wrong number of arguments"
        echo "redis-server without tls"
        return
    fi
    if ! command -v redis-cli &> /dev/null; then
        echo "redis-cli not found"
        return
    fi
    if ! redis-cli --tls --version &> /dev/null; then
        # "Unrecognized option or bad number of args"
        echo "redis-cli without tls"
        return
    fi
}

start_rediss_server() {
    local port="$1"
    local password="${2:-}"
    local ca_key="ca.key"
    local ca_cert="ca.crt"
    local ca_serial="ca.txt"
    local server_key="server.key"
    local server_cert="server.crt"
    local client_key="client.key"
    local client_cert="client.crt"

    [ -f "${ca_key}" ] || openssl genrsa -out "${ca_key}" 4096 &>openssl.log
    openssl req \
        -x509 -new -nodes -sha256 \
        -key "${ca_key}" \
        -days 3650 \
        -subj '/O=Redis Test/CN=Certificate Authority' \
        -out "${ca_cert}" &>openssl.log

    [ -f "${server_key}" ] || openssl genrsa -out "${server_key}" 2048 &>openssl.log
    openssl req \
        -new -sha256 \
        -subj "/O=Redis Test/CN=Server-only" \
        -key "${server_key}" | \
        openssl x509 \
            -req -sha256 \
            -CA "${ca_cert}" \
            -CAkey "${ca_key}" \
            -CAserial "${ca_serial}" \
            -CAcreateserial \
            -days 365 \
            -out "${server_cert}" &>openssl.log

    [ -f "${client_key}" ] || openssl genrsa -out "${client_key}" 2048 &>openssl.log
    openssl req \
        -new -sha256 \
        -subj "/O=Redis Test/CN=Client-only" \
        -key "${client_key}" | \
        openssl x509 \
            -req -sha256 \
            -CA "${ca_cert}" \
            -CAkey "${ca_key}" \
            -CAserial "${ca_serial}" \
            -CAcreateserial \
            -days 365 \
            -out "${client_cert}" &>openssl.log

    redis-server --bind localhost --tls-port "${port}" --port 0 \
                 --tls-cert-file "${server_cert}" --tls-key-file "${server_key}" --tls-ca-cert-file "${ca_cert}" >/dev/null &
    # Wait for server start.
    i=0
    while [ $i -lt 100 ] && ! redis-cli --tls -p "${port}" --cert "${client_cert}" --key "${client_key}" --cacert "${ca_cert}" ping &>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done

    if [ -n "${password}" ]; then
        redis-cli --tls -p "${port}" --cert "${client_cert}" --key "${client_key}" --cacert "${ca_cert}" config set requirepass "${password}" &>/dev/null
    fi
}

SUITE_secondary_rediss_SETUP() {
    unset CCACHE_NODIRECT

    generate_code 1 test.c
}

expect_number_of_rediss_cache_entries() {
    local expected=$1
    local url=${2/rediss/redis}  # use --tls parameter instead of url ("unknown scheme")
    local actual

    actual=$(redis-cli --tls -u "$url" --cert "client.crt" --key "client.key" --cacert "ca.crt" keys "ccache:*" 2>/dev/null | wc -l)
    if [ "$actual" -ne "$expected" ]; then
        test_failed_internal "Found $actual (expected $expected) entries in $url"
    fi
}

SUITE_secondary_rediss() {
    # -------------------------------------------------------------------------
    TEST "Base case"

    port=7777
    redis_url="rediss://localhost:${port}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}|cacert=ca.crt|cert=client.crt|key=client.key"

    start_rediss_server "${port}"
    function expect_number_of_redis_cache_entries()
    {
       expect_number_of_rediss_cache_entries "$@"
    }

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    # -------------------------------------------------------------------------
    TEST "Password"

    port=7777
    password=secret
    redis_url="rediss://${password}@localhost:${port}"
    export CCACHE_SECONDARY_STORAGE="${redis_url}|cacert=ca.crt|cert=client.crt|key=client.key"

    start_rediss_server "${port}" "${password}"
    function expect_number_of_redis_cache_entries()
    {
       expect_number_of_rediss_cache_entries "$@"
    }

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 0
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 1
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 2
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE -C >/dev/null
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest

    $CCACHE_COMPILE -c test.c
    expect_stat 'cache hit (direct)' 2
    expect_stat 'cache miss' 1
    expect_stat 'files in cache' 0
    expect_number_of_redis_cache_entries 2 "$redis_url" # result + manifest
}
