#include <unistd.h>
#include "background.h"

static void* flush_thread_main(void *in);
static void* unmap_thread_main(void *in);
typedef struct {
    bloom_config *config;
    bloom_filtmgr *mgr;
    int *should_run;
} background_thread_args;

/**
 * Helper macro to pack and unpack the arguments
 * to the thread, and free the memory.
 */
# define PACK_ARGS() {                  \
    args = malloc(sizeof(background_thread_args));  \
    args->config = config;              \
    args->mgr = mgr;                    \
    args->should_run = should_run;      \
}
# define UNPACK_ARGS() {                \
    background_thread_args *args = in;  \
    config = args->config;              \
    mgr = args->mgr;                    \
    should_run = args->should_run;      \
    free(args);                         \
}


/**
 * Starts a flushing thread which on every
 * configured flush interval, flushes all the filters.
 * @arg config The configuration
 * @arg mgr The filter manager to use
 * @arg should_run Pointer to an integer that is set to 0 to
 * indicate the thread should exit.
 * @arg t The output thread
 * @return 1 if the thread was started
 */
int start_flush_thread(bloom_config *config, bloom_filtmgr *mgr, int *should_run, pthread_t *t) {
    // Return if we are not scheduled
    if(config->flush_interval <= 0) {
        return 0;
    }

    // Start thread
    background_thread_args *args;
    PACK_ARGS();
    pthread_create(t, NULL, flush_thread_main, args);
    return 1;
}

/**
 * Starts a cold unmap thread which on every
 * cold interval unamps cold filtesr.
 * @arg config The configuration
 * @arg mgr The filter manager to use
 * @arg should_run Pointer to an integer that is set to 0 to
 * indicate the thread should exit.
 * @arg t The output thread
 * @return 1 if the thread was started
 */
int start_cold_unmap_thread(bloom_config *config, bloom_filtmgr *mgr, int *should_run, pthread_t *t) {
    // Return if we are not scheduled
    if(config->cold_interval <= 0) {
        return 0;
    }

    // Start thread
    background_thread_args *args;
    PACK_ARGS();
    pthread_create(t, NULL, unmap_thread_main, args);
    return 1;
}


static void* flush_thread_main(void *in) {
    bloom_config *config;
    bloom_filtmgr *mgr;
    int *should_run;
    UNPACK_ARGS();

    syslog(LOG_INFO, "Flush thread started. Interval: %d seconds.", config->flush_interval);
    unsigned int ticks = 0;
    while (*should_run) {
        sleep(1);
        if ((++ticks % config->flush_interval) == 0 && *should_run) {
            // List all the filters
            syslog(LOG_INFO, "Scheduled flush started.");
            bloom_filter_list_head *head;
            int res = filtmgr_list_filters(mgr, &head);
            if (res != 0) {
                syslog(LOG_WARNING, "Failed to list filters for flushing!");
                continue;
            }

            // Flush all, ignore errors since
            // filters might get deleted in the process
            bloom_filter_list *node = head->head;
            while (node) {
                filtmgr_flush_filter(mgr, node->filter_name);
                node = node->next;
            }

            // Cleanup
            filtmgr_cleanup_list(head);
        }
    }
    return NULL;
}

static void* unmap_thread_main(void *in) {
    bloom_config *config;
    bloom_filtmgr *mgr;
    int *should_run;
    UNPACK_ARGS();

    syslog(LOG_INFO, "Cold unmap thread started. Interval: %d seconds.", config->cold_interval);
    unsigned int ticks = 0;
    while (*should_run) {
        sleep(1);
        if ((++ticks % config->cold_interval) == 0 && *should_run) {
            // List the cold filters
            syslog(LOG_INFO, "Cold unmap started.");
            bloom_filter_list_head *head;
            int res = filtmgr_list_cold_filters(mgr, &head);
            if (res != 0) {
                continue;
            }

            // Close the filters, save memory
            syslog(LOG_INFO, "Cold filter count: %d", head->size);
            bloom_filter_list *node = head->head;
            while (node) {
                syslog(LOG_INFO, "Unmapping filter '%s' for being cold.", node->filter_name);
                filtmgr_unmap_filter(mgr, node->filter_name);
                node = node->next;
            }

            // Cleanup
            filtmgr_cleanup_list(head);
        }
    }
    return NULL;
}


