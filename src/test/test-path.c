/* SPDX-License-Identifier: LGPL-2.1+ */

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "alloc-util.h"
#include "all-units.h"
#include "fd-util.h"
#include "fs-util.h"
#include "macro.h"
#include "manager.h"
#include "mkdir.h"
#include "path-util.h"
#include "rm-rf.h"
#include "string-util.h"
#include "strv.h"
#include "tests.h"
#include "unit.h"
#include "util.h"

typedef void (*test_function_t)(Manager *m);

static int setup_test(Manager **m) {
        char **tests_path = STRV_MAKE("exists", "existsglobFOOBAR", "changed", "modified", "unit",
                                      "directorynotempty", "makedirectory");
        char **test_path;
        Manager *tmp = NULL;
        int r;

        assert_se(m);

        r = enter_cgroup_subroot(NULL);
        if (r == -ENOMEDIUM)
                return log_tests_skipped("cgroupfs not available");

        r = manager_new(UNIT_FILE_USER, MANAGER_TEST_RUN_BASIC, &tmp);
        if (manager_errno_skip_test(r))
                return log_tests_skipped_errno(r, "manager_new");
        assert_se(r >= 0);
        assert_se(manager_startup(tmp, NULL, NULL) >= 0);

        STRV_FOREACH(test_path, tests_path) {
                _cleanup_free_ char *p = NULL;

                p = strjoin("/tmp/test-path_", *test_path);
                assert_se(p);

                (void) rm_rf(p, REMOVE_ROOT|REMOVE_PHYSICAL);
        }

        *m = tmp;

        return 0;
}

static void shutdown_test(Manager *m) {
        assert_se(m);

        manager_free(m);
}

static Service *service_for_path(Manager *m, Path *path, const char *service_name) {
        _cleanup_free_ char *tmp = NULL;
        Unit *service_unit = NULL;

        assert_se(m);
        assert_se(path);

        if (!service_name) {
                assert_se(tmp = strreplace(UNIT(path)->id, ".path", ".service"));
                service_unit = manager_get_unit(m, tmp);
        } else
                service_unit = manager_get_unit(m, service_name);
        assert_se(service_unit);

        return SERVICE(service_unit);
}

static void check_states(Manager *m, Path *path, Service *service, PathState path_state, ServiceState service_state) {
        usec_t ts;
        usec_t timeout = 2 * USEC_PER_SEC;

        assert_se(m);
        assert_se(service);

        ts = now(CLOCK_MONOTONIC);

        while (path->result != PATH_SUCCESS || service->result != SERVICE_SUCCESS ||
               path->state != path_state || service->state != service_state) {
                usec_t n;
                int r;

                r = sd_event_run(m->event, 100 * USEC_PER_MSEC);
                assert_se(r >= 0);

                printf("%s: state = %s; result = %s \n",
                                UNIT(path)->id,
                                path_state_to_string(path->state),
                                path_result_to_string(path->result));
                printf("%s: state = %s; result = %s \n",
                                UNIT(service)->id,
                                service_state_to_string(service->state),
                                service_result_to_string(service->result));

                n = now(CLOCK_MONOTONIC);
                if (ts + timeout < n) {
                        log_error("Test timeout when testing %s", UNIT(path)->id);
                        exit(EXIT_FAILURE);
                }
        }
}

static void test_path_exists(Manager *m) {
        const char *test_path = "/tmp/test-path_exists";
        Unit *unit = NULL;
        Path *path = NULL;
        Service *service = NULL;

        assert_se(m);

        assert_se(manager_load_startable_unit_or_warn(m, "path-exists.path", NULL, &unit) >= 0);

        path = PATH(unit);
        service = service_for_path(m, path, NULL);

        assert_se(unit_start(unit) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(touch(test_path) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        /* Service restarts if file still exists */
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        assert_se(rm_rf(test_path, REMOVE_ROOT|REMOVE_PHYSICAL) == 0);
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(unit_stop(unit) >= 0);
}

static void test_path_existsglob(Manager *m) {
        const char *test_path = "/tmp/test-path_existsglobFOOBAR";
        Unit *unit = NULL;
        Path *path = NULL;
        Service *service = NULL;

        assert_se(m);

        assert_se(manager_load_startable_unit_or_warn(m, "path-existsglob.path", NULL, &unit) >= 0);

        path = PATH(unit);
        service = service_for_path(m, path, NULL);

        assert_se(unit_start(unit) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(touch(test_path) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        /* Service restarts if file still exists */
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        assert_se(rm_rf(test_path, REMOVE_ROOT|REMOVE_PHYSICAL) == 0);
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(unit_stop(unit) >= 0);
}

static void test_path_changed(Manager *m) {
        const char *test_path = "/tmp/test-path_changed";
        FILE *f;
        Unit *unit = NULL;
        Path *path = NULL;
        Service *service = NULL;

        assert_se(m);

        assert_se(manager_load_startable_unit_or_warn(m, "path-changed.path", NULL, &unit) >= 0);

        path = PATH(unit);
        service = service_for_path(m, path, NULL);

        assert_se(unit_start(unit) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(touch(test_path) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        /* Service does not restart if file still exists */
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        f = fopen(test_path, "w");
        assert_se(f);
        fclose(f);

        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        (void) rm_rf(test_path, REMOVE_ROOT|REMOVE_PHYSICAL);
        assert_se(unit_stop(unit) >= 0);
}

static void test_path_modified(Manager *m) {
        _cleanup_fclose_ FILE *f = NULL;
        const char *test_path = "/tmp/test-path_modified";
        Unit *unit = NULL;
        Path *path = NULL;
        Service *service = NULL;

        assert_se(m);

        assert_se(manager_load_startable_unit_or_warn(m, "path-modified.path", NULL, &unit) >= 0);

        path = PATH(unit);
        service = service_for_path(m, path, NULL);

        assert_se(unit_start(unit) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(touch(test_path) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        /* Service does not restart if file still exists */
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        f = fopen(test_path, "w");
        assert_se(f);
        fputs("test", f);

        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        (void) rm_rf(test_path, REMOVE_ROOT|REMOVE_PHYSICAL);
        assert_se(unit_stop(unit) >= 0);
}

static void test_path_unit(Manager *m) {
        const char *test_path = "/tmp/test-path_unit";
        Unit *unit = NULL;
        Path *path = NULL;
        Service *service = NULL;

        assert_se(m);

        assert_se(manager_load_startable_unit_or_warn(m, "path-unit.path", NULL, &unit) >= 0);

        path = PATH(unit);
        service = service_for_path(m, path, "path-mycustomunit.service");

        assert_se(unit_start(unit) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(touch(test_path) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        assert_se(rm_rf(test_path, REMOVE_ROOT|REMOVE_PHYSICAL) == 0);
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(unit_stop(unit) >= 0);
}

static void test_path_directorynotempty(Manager *m) {
        const char *test_path = "/tmp/test-path_directorynotempty/";
        Unit *unit = NULL;
        Path *path = NULL;
        Service *service = NULL;

        assert_se(m);

        assert_se(manager_load_startable_unit_or_warn(m, "path-directorynotempty.path", NULL, &unit) >= 0);

        path = PATH(unit);
        service = service_for_path(m, path, NULL);

        assert_se(access(test_path, F_OK) < 0);

        assert_se(unit_start(unit) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        /* MakeDirectory default to no */
        assert_se(access(test_path, F_OK) < 0);

        assert_se(mkdir_p(test_path, 0755) >= 0);
        assert_se(touch(strjoina(test_path, "test_file")) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        /* Service restarts if directory is still not empty */
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_RUNNING, SERVICE_RUNNING);

        assert_se(rm_rf(test_path, REMOVE_ROOT|REMOVE_PHYSICAL) == 0);
        assert_se(unit_stop(UNIT(service)) >= 0);
        check_states(m, path, service, PATH_WAITING, SERVICE_DEAD);

        assert_se(unit_stop(unit) >= 0);
}

static void test_path_makedirectory_directorymode(Manager *m) {
        const char *test_path = "/tmp/test-path_makedirectory/";
        Unit *unit = NULL;
        struct stat s;

        assert_se(m);

        assert_se(manager_load_startable_unit_or_warn(m, "path-makedirectory.path", NULL, &unit) >= 0);

        assert_se(access(test_path, F_OK) < 0);

        assert_se(unit_start(unit) >= 0);

        /* Check if the directory has been created */
        assert_se(access(test_path, F_OK) >= 0);

        /* Check the mode we specified with DirectoryMode=0744 */
        assert_se(stat(test_path, &s) >= 0);
        assert_se((s.st_mode & S_IRWXU) == 0700);
        assert_se((s.st_mode & S_IRWXG) == 0040);
        assert_se((s.st_mode & S_IRWXO) == 0004);

        assert_se(unit_stop(unit) >= 0);
        (void) rm_rf(test_path, REMOVE_ROOT|REMOVE_PHYSICAL);
}

int main(int argc, char *argv[]) {
        static const test_function_t tests[] = {
                test_path_exists,
                test_path_existsglob,
                test_path_changed,
                test_path_modified,
                test_path_unit,
                test_path_directorynotempty,
                test_path_makedirectory_directorymode,
                NULL,
        };

        _cleanup_free_ char *test_path = NULL;
        _cleanup_(rm_rf_physical_and_freep) char *runtime_dir = NULL;

        umask(022);

        test_setup_logging(LOG_INFO);

        assert_se(get_testdata_dir("test-path", &test_path) >= 0);
        assert_se(set_unit_path(test_path) >= 0);
        assert_se(runtime_dir = setup_fake_runtime_dir());

        for (const test_function_t *test = tests; test && *test; test++) {
                Manager *m = NULL;
                int r;

                /* We create a clean environment for each test */
                r = setup_test(&m);
                if (r != 0)
                        return r;

                (*test)(m);

                shutdown_test(m);
        }

        return 0;
}
