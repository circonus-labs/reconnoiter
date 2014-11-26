Looking at logs:

    ::noit_log | ::print struct _noit_log_stream
    ::noit_log | ::noit_print_membuf_log
    ::noit_log internal | ::noit_print_membuf_log

Looking at events using `::print_event`

    > 0x10451f8e0 ::print_event
    0x10451f8e0 = {
        callback = noit_listener_acceptor
        closure  = 0x104a42100
        fd       = 43 (67t)
        opset    = snowthd`_eventer_POSIX_fd_opset (0)
        mask     = READ,EXCEPTION
    }

Looking at timed events:

    *timed_events ::walk noit_skiplist | ::print struct _event
    ::eventer_timed | ::print_event

Looking at file descriptor events:

    ::walk eventer_fds | ::print struct _event
    ::eventer_fd | ::print_event

    ::eventer_fd 43 | ::print_event

Looking at eventer jobqs:

    all_queues ::walk noit_hash | ::print eventer_jobq_t queue_name
    ::eventer_jobq | ::print eventer_jobq_t queue_name

    ::eventer_jobq default_back_queue | ::print eventer_jobq_t
