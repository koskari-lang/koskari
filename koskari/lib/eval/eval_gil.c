

/**
   Return 1 if another thread requested this thread drop the GIL, else 0
*/
static inline int koskari_gil_drop_requested(void) {
#if defined(Py_GIL_DISABLED) && Py_GIL_DISABLED
    return 0;
#else

#if PY_MINOR_VERSION >= 13
    /*
        struct _ts through eval_breaker (GIL drop is bit 0):
        https://github.com/python/cpython/blob/v3.13.0/Include/cpython/pystate.h
        https://github.com/python/cpython/blob/v3.13.0/Include/internal/pycore_ceval.h
    */
    struct koskari_ts_313 {
        PyThreadState *prev;
        PyThreadState *next;
        PyInterpreterState *interp;
        uintptr_t eval_breaker;
    };

    PyThreadState *const tstate = PyThreadState_Get();
    if (! tstate)
    return 0;

    return (((struct koskari_ts_313 *) tstate)->eval_breaker
            & (uintptr_t) 1) != 0;

#elif PY_MINOR_VERSION == 12
    /*
        struct _ceval_state:
        https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_ceval_state.h
        struct _is (gc_opaque matches struct _gc_runtime_state size):
        https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_interp.h
        https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_gc.h
    */
    struct koskari_ceval_312 {
        int eval_breaker;
        int gil_drop_request;
        int recursion_limit;
    };

    struct koskari_pythreads_312 {
        uint64_t next_unique_id;
        PyThreadState *head;
        long count;
        size_t stacksize;
    };

    struct koskari_is_312 {
        PyInterpreterState *next;
        int64_t id;
        int64_t id_refcount;
        int requires_idref;
        void *id_mutex;
        int _initialized;
        int finalizing;
        uint64_t monitoring_version;
        uint64_t last_restart_version;
        struct koskari_pythreads_312 threads;
        void *runtime;
        uintptr_t _finalizing;
        unsigned char gc_opaque[240];
        PyObject *sysdict;
        PyObject *builtins;
        struct koskari_ceval_312 ceval;
    };

    PyInterpreterState *interp;

    PyThreadState *const tstate = PyThreadState_Get();
    if (! tstate || ! tstate->interp)
        return 0;

    interp = tstate->interp;
    return ((struct koskari_is_312 *) interp)->ceval.gil_drop_request != 0;

#elif PY_MINOR_VERSION == 11
    /*
        struct _ceval_state and struct _is:
        https://github.com/python/cpython/blob/v3.11.0/Include/internal/pycore_interp.h
    */
    struct koskari_ceval_311 {
        int recursion_limit;
        int eval_breaker;
        int gil_drop_request;
    };

    struct koskari_pythreads_311 {
        uint64_t next_unique_id;
        PyThreadState *head;
        long count;
        size_t stacksize;
    };

    struct koskari_is_311 {
        PyInterpreterState *next;
        struct koskari_pythreads_311 threads;
        void *runtime;
        int64_t id;
        int64_t id_refcount;
        int requires_idref;
        void *id_mutex;
        int _initialized;
        int finalizing;
        _Bool _static;
        struct koskari_ceval_311 ceval;
    };

    PyInterpreterState *interp;

    PyThreadState *const tstate = PyThreadState_Get();
    if (! tstate || ! tstate->interp)
        return 0;

    interp = tstate->interp;
    return ((struct koskari_is_311 *) interp)->ceval.gil_drop_request != 0;

#elif PY_MINOR_VERSION == 10
    /*
        struct _ceval_state and struct _is:
        https://github.com/python/cpython/blob/v3.10.0/Include/internal/pycore_interp.h
    */
    struct koskari_ceval_310 {
        int recursion_limit;
        int eval_breaker;
        int gil_drop_request;
    };

    struct koskari_is_310 {
        PyInterpreterState *next;
        PyThreadState *tstate_head;
        void *runtime;
        int64_t id;
        int64_t id_refcount;
        int requires_idref;
        void *id_mutex;
        int finalizing;
        struct koskari_ceval_310 ceval;
    };

    PyInterpreterState *interp;

    PyThreadState *const tstate = PyThreadState_Get();
    if (! tstate || ! tstate->interp)
        return 0;

    interp = tstate->interp;
    return ((struct koskari_is_310 *) interp)->ceval.gil_drop_request != 0;

#elif PY_MINOR_VERSION == 9
    /*
        struct _ceval_state and struct _is:
        https://github.com/python/cpython/blob/v3.9.0/Include/internal/pycore_interp.h
    */
    struct koskari_ceval_39 {
        int recursion_limit;
        int eval_breaker;
        int gil_drop_request;
    };

    struct koskari_is_39 {
        PyInterpreterState *next;
        PyThreadState *tstate_head;
        void *runtime;
        int64_t id;
        int64_t id_refcount;
        int requires_idref;
        void *id_mutex;
        int finalizing;
        struct koskari_ceval_39 ceval;
    };

    PyInterpreterState *interp;

    PyThreadState *const tstate = PyThreadState_Get();
    if (! tstate || ! tstate->interp)
        return 0;

    interp = tstate->interp;
    return ((struct koskari_is_39 *) interp)->ceval.gil_drop_request != 0;

#else
    // we can't tell if someone requested a GIL drop, so we'll always return 1
    return 1;

#endif

#endif

}
