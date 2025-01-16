#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <random>

#include "bytefs_utils.h"
#include "utils.h"
#include "ftl.h"
#include "simulator_clock.h"

using std::to_string;

/* a series of macro for debug : reduce printk number in pre-compilation level */
#define TEST_SSD_NODEBUG        0
#define TEST_SSD_RANDRW_VERIFY  1
#define TEST_SSD_DEBUG          TEST_SSD_RANDRW_VERIFY

#define TEST_SSD_RANDW_LOC_NUM  1280
#define TEST_SSD_RAND_ROUNDS    50

extern ssd gdev;
extern sim_clock* the_clock_pt;

extern bool promotion_enable;
extern bool tpp_enable;
extern bool write_log_enable;
extern bool use_macsim;
extern bool pinatrace_drive;

static int bytefs_start_thread(pthread_t *thread_id, void *thread_args, void *(func)(void *), string thread_name) {
    int retval;
    retval = pthread_create(thread_id, nullptr, func, thread_args);
    if (retval != 0) {
        bytefs_err("Failed to create %s thread", thread_name.c_str());
        retval = -1;
    } else {
        pthread_setname(*thread_id, thread_name.c_str());
        //int cpu_idx = pthread_bind(*thread_id);
        bytefs_log("Creating %s thread", thread_name.c_str());
    }
    return retval;
}

static int bytefs_cancel_thread(pthread_t *thread_id, string thread_name) {
    int retval = pthread_cancel(*thread_id);
    if (retval == 0) {
        bytefs_log("Successfully terminated %s thread", thread_name.c_str());
        return 0;
    }
    bytefs_log("Termination of %s thread failed with retcode %d", thread_name.c_str(), retval);
    return retval;
}

int bytefs_start_threads(void) {
    ssd *ssd = &gdev;
    int retval;

    if (!ssd->thread_args)
        ssd->thread_args = (ftl_thread_info *) malloc(sizeof(struct ftl_thread_info));
    ssd->thread_args->num_poller = 1;
    ssd->thread_args->to_ftl = ssd->to_ftl;
    ssd->thread_args->to_poller = ssd->to_poller;

    bytefs_log("ByteFS start threads");

    if (pinatrace_drive)
    {
        ssd->simulator_timer_id = new pthread_t;
        bytefs_start_thread(ssd->simulator_timer_id, nullptr, simulator_timer_thread, "event timer");
    }
    
    ssd->ftl_thread_id = new pthread_t;
    bytefs_start_thread(ssd->ftl_thread_id, ssd->thread_args, ftl_thread, "ftl");

    // ssd->polling_thread_id = new pthread_t;
    // bytefs_start_thread(ssd->polling_thread_id, ssd->thread_args, request_poller_thread, "request poller");

    if (write_log_enable) {
        bytefs_log("Initizing %ld log writer threads", ssd->n_log_writer_threads);
        ssd->log_writer_thread_id = new pthread_t[ssd->n_log_writer_threads];
        for (uint64_t log_writer_thread_idx = 0; log_writer_thread_idx < ssd->n_log_writer_threads; log_writer_thread_idx++) {
            bytefs_start_thread(&ssd->log_writer_thread_id[log_writer_thread_idx], nullptr, 
                                log_writer_thread, ("log writer #" + to_string(log_writer_thread_idx)).c_str());
        }
    }
    
    if (promotion_enable || tpp_enable) {
        bytefs_log("Initizing %ld promotion threads", ssd->n_promotion_threads);
        ssd->promotion_thread_id = new pthread_t[ssd->n_promotion_threads];
        for (uint64_t promotion_thread_idx = 0; promotion_thread_idx < ssd->n_promotion_threads; promotion_thread_idx++) {
            bytefs_start_thread(&ssd->promotion_thread_id[promotion_thread_idx], nullptr, 
                                promotion_thread, ("promotion #" + to_string(promotion_thread_idx)).c_str());
        }
    }
    else
    {
        the_clock_pt->wait_without_events(ThreadType::Page_promotion_thread, 0);
    }
    
    return 0;
}

int bytefs_stop_threads(void) {
    ssd *ssd = &gdev;

    // kill the thread first
    bytefs_log("ByteFS stopping threads");

    bytefs_cancel_thread(ssd->ftl_thread_id, "ftl");
    delete ssd->ftl_thread_id;

    bytefs_cancel_thread(ssd->simulator_timer_id, "event timer");
    delete ssd->simulator_timer_id;

    // bytefs_cancel_thread(ssd->polling_thread_id, "request polling");
    // delete ssd->polling_thread_id;

    if (write_log_enable) {
        for (uint64_t log_writer_thread_idx = 0; log_writer_thread_idx < ssd->n_log_writer_threads; log_writer_thread_idx++)
            bytefs_cancel_thread(&ssd->log_writer_thread_id[log_writer_thread_idx],
                                ("log writer #" + to_string(log_writer_thread_idx)).c_str());
        delete[] ssd->log_writer_thread_id;
    }

    if (promotion_enable || tpp_enable) {
        for (uint64_t promotion_thread_idx = 0; promotion_thread_idx < ssd->n_promotion_threads; promotion_thread_idx++)
            //bytefs_cancel_thread(&ssd->promotion_thread_id[promotion_thread_idx],
            //                    ("promotion #" + to_string(promotion_thread_idx)).c_str());
        delete[] ssd->promotion_thread_id;
    }

    return 0;
}

void bytefs_stop_threads_gracefully(void) {
    ssd *ssd = &gdev;
    ssd->terminate_flag = 1;

    // pthread_join(*(ssd->polling_thread_id), nullptr);
    // bytefs_log("Request polling thread terminated");

    pthread_join(*(ssd->ftl_thread_id), nullptr);
    bytefs_log("FTL thread terminated");


    if (write_log_enable) {
        for (uint64_t i = 0; i < ssd->n_log_writer_threads; i++)
            pthread_join(ssd->log_writer_thread_id[i], nullptr);
        bytefs_log("Log writer threads terminated");
    }

    if (promotion_enable || tpp_enable) {
        for (uint64_t i = 0; i < ssd->n_promotion_threads; i++)
            //pthread_join(ssd->promotion_thread_id[i], nullptr);
        bytefs_log("Promotion threads terminated");
    }

    
    // bytefs_cancel_thread(ssd->simulator_timer_id, "event timer");
    // delete ssd->simulator_timer_id;
    if (pinatrace_drive)
    {
        the_clock_pt->terminate_flag = true;
        the_clock_pt->workers_cv.notify_all();
        pthread_join(*(ssd->simulator_timer_id), nullptr);
        bytefs_log("Event timer thread terminated");
    }
}
