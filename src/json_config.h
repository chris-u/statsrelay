#ifndef STATSRELAY_JSON_CONFIG_H
#define STATSRELAY_JSON_CONFIG_H

#include "./list.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct additional_config {
    /**
     * A string to prepend/append to each metric going through a duplicate block.
     * No dot (.) is added - this is a raw string.
     */
    char* prefix;
    size_t prefix_len;
    char* suffix;
    size_t suffix_len;
    /**
     * A PCRE compatible regex which will only allow these metrics through this
     * duplicate endpoint.
     */
    char* ingress_filter;

    /**
     *  A PCRE compatible regex which will drop metrics matching this regex to the
     *  duplicate endpoint.
     */
    char* ingress_blacklist;

    /**
     * sampling_threshold: start sampling messages received at a rate greater than
     * this quantity over the sampling_window
     */
    int sampling_threshold;

    /**
     * sampling_window: number of seconds to do sampling before flushing internally
     */
    int sampling_window;

    /**
     * max_counters: Max number of unique counters we will allow (and flush) before
     * flagging and dropping counters
     */
    int max_counters;

    /**
     * max_timers: Max number of unique timer we will allows (and flush) before
     * flagging and dropping timers
     */
    int max_timers;

    /**
     * max_gauges: Max number of unique gauges we will allow (and flush) before
     * flagging and dropping gauges
     */
    int max_gauges;

    /**
     * timer_sampling_threshold: start sampling messages received at a rate greater than
     * this quantity over the timer_sampling_window
     */
    int timer_sampling_threshold;

    /**
     * timer_sampling_window: number of seconds to sample timers before flushing internally
     */
    int timer_sampling_window;

    /**
     * timer_flush_min_max: If timer sampling has been enabled, we can optionally flush
     * the true upper and lower values for the timer every sampler flush interval
     */
    bool timer_flush_min_max;

    /**
     * reservoir_size: size of the reservoir, number of samples kept in memory for every
     * timer being sampled. only applies to samplers
     */
    int reservoir_size;

    /**
     * gauge_sampling_threshold: start sampling messages received at a rate greater than
     * this quantity over the gauge_sampling_window
     */
    int gauge_sampling_threshold;

    /**
     * gauges_sampling_window: number of seconds to sample gauges before flushing internally
     */
    int gauge_sampling_window;

    /**
     * hm_key_expiration_frequency_in_seconds: frequency with which purging of expired items in hashmap
     * happens (in seconds) (currently only applies to timer sampling hashmap)
     */
    int hm_key_expiration_frequency_in_seconds;

    /**
     * hm_key_ttl_in_seconds: TTL of hashmap key in seconds (currently only applies to timer sampling hashmap)
     */
    int hm_key_ttl_in_seconds;

    /**
     * A list of host:port combos where to forward traffic, consistently hashed.
     */
    list_t ring;
};

struct proto_config {
    bool initialized;
    bool send_health_metrics;
    char *bind;
    bool enable_validation;
    bool enable_tcp_cork;
    bool auto_reconnect; /* drop connections to backend and reconnect on full buffer */
    double reconnect_threshold; /* initiate auto reconnect when send buffer hits this threshold */
    uint64_t max_send_queue;
    list_t ring;
    list_t dupl; /* struct additional_config */
    list_t sstats; /* struct additional_config */
};

struct config {
    struct proto_config statsd_config;
};


static const char default_config[] = "/etc/statsrelay.json";

struct config* parse_json_config(FILE *input);

// release the memory associated with a config
void destroy_json_config(struct config *);

#endif  // STATSRELAY_JSON_CONFIG_H
