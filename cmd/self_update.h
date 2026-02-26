#ifndef NATURE_CMD_SELF_UPDATE_H
#define NATURE_CMD_SELF_UPDATE_H

#include "config/config.h"
#include "src/build/config.h"
#include "utils/exec.h"
#include "utils/helper.h"
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static inline void trim_line_endings(char *text) {
    if (!text) {
        return;
    }

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len--;
    }
}

static inline char *fetch_latest_release_tag() {
    char *workdir = get_workdir();
    char *command = "curl -fsSL https://api.github.com/repos/nature-lang/nature/releases/latest | grep '\"tag_name\"' | head -n1 | sed -E 's/.*\"([^\"]+)\".*/\\1/'";
    return command_output(workdir, command);
}

static inline char *host_os_string() {
#ifdef __DARWIN
    return "darwin";
#elif __LINUX
    return "linux";
#else
    return NULL;
#endif
}

static inline char *host_arch_string() {
#ifdef __AMD64
    return "amd64";
#elif __ARM64
    return "arm64";
#elif __RISCV64
    return "riscv64";
#else
    return NULL;
#endif
}

static inline char *fetch_latest_release_asset_url(char *os, char *arch) {
    char *workdir = get_workdir();
    char *command = dsprintf("curl -fsSL https://api.github.com/repos/nature-lang/nature/releases/latest | grep 'browser_download_url' | grep -i '%s-%s.*tar.gz' | head -n1 | cut -d '\"' -f 4", os, arch);
    char *url = command_output(workdir, command);
    free(command);
    return url;
}

static inline bool can_write_to_path(char *path) {
    return access(path, W_OK) == 0;
}

static inline char *detect_install_root(char *argv0) {
    char resolved[PATH_MAX] = {0};
    if (argv0 && realpath(argv0, resolved) != NULL) {
        char *exe_path = strdup(resolved);
        char *bin_dir = path_dir(exe_path);
        char *root_dir = path_dir(bin_dir);

        char *nature_bin = path_join(strdup(root_dir), "bin/nature");
        bool valid_layout = ends_with(bin_dir, "/bin") && file_exists(nature_bin);
        free(nature_bin);

        if (valid_layout) {
            free(exe_path);
            return root_dir;
        }

        free(exe_path);
        free(root_dir);
    }

    return strdup(NATURE_ROOT);
}

static inline bool prompt_confirm_update() {
    char input[8] = {0};
    printf("Proceed with update? [y/N]: ");
    if (!fgets(input, sizeof(input), stdin)) {
        return false;
    }
    return input[0] == 'y' || input[0] == 'Y';
}

static inline int apply_self_update(char *install_root, char *asset_url, bool auto_yes) {
    if (!install_root || !asset_url) {
        printf("Update failed: missing install root or asset url.\n");
        return 1;
    }

    char *parent_dir = path_dir(strdup(install_root));
    char *target_name = file_name(install_root);
    char *backup_path = dsprintf("%s.bak", install_root);
    bool need_sudo = !can_write_to_path(parent_dir) || (dir_exists(install_root) && !can_write_to_path(install_root));

    if (!auto_yes && !prompt_confirm_update()) {
        printf("Update canceled.\n");
        free(parent_dir);
        free(backup_path);
        return 0;
    }

    char *prefix = need_sudo ? "sudo " : "";
    char *script = dsprintf(
            "set -e; "
            "UPDATE_TMP_DIR=$(mktemp -d); "
            "trap 'rm -rf \"$UPDATE_TMP_DIR\"' EXIT; "
            "ASSET=\"$UPDATE_TMP_DIR/nature-update.tar.gz\"; "
            "curl -fL '%s' -o \"$ASSET\"; "
            "mkdir -p \"$UPDATE_TMP_DIR/extracted\"; "
            "tar -xzf \"$ASSET\" -C \"$UPDATE_TMP_DIR/extracted\"; "
            "if [ ! -x \"$UPDATE_TMP_DIR/extracted/%s/bin/nature\" ]; then "
            "  echo 'invalid release asset layout'; exit 1; "
            "fi; "
            "%smkdir -p '%s'; "
            "if [ -d '%s' ]; then %srm -rf '%s'; %smv '%s' '%s'; fi; "
            "%smv \"$UPDATE_TMP_DIR/extracted/%s\" '%s'",
            asset_url,
            target_name,
            prefix,
            parent_dir,
            backup_path,
            prefix,
            backup_path,
            prefix,
            install_root,
            backup_path,
            prefix,
            target_name,
            install_root);

            int status = system(script);

    free(parent_dir);
    free(backup_path);
    free(script);

    if (status != 0) {
        printf("Update failed. Existing installation is kept at %s (or backup at %s).\n", install_root, install_root);
        return 1;
    }

    printf("Update completed successfully.\n");
    return 0;
}

static inline void print_self_update_usage() {
    printf("Usage: nature self-update [--check] [--yes] [--force]\n");
}

void cmd_self_update_entry(int argc, char **argv) {
    struct option long_options[] = {
            {"check", no_argument, NULL, 1},
            {"yes", no_argument, NULL, 2},
            {"force", no_argument, NULL, 3},
            {NULL, 0, NULL, 0}};

    int option_index = 0;
    int c;
    bool check_only = false;
    bool auto_yes = false;
    bool force_update = false;

    while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
        switch (c) {
            case 1: {
                check_only = true;
                break;
            }
            case 2: {
                auto_yes = true;
                break;
            }
            case 3: {
                force_update = true;
                break;
            }
            default: {
                print_self_update_usage();
                return;
            }
        }
    }

    char *latest_tag = fetch_latest_release_tag();
    if (!latest_tag) {
        printf("Failed to check latest version. Ensure network access and curl are available.\n");
        return;
    }

    trim_line_endings(latest_tag);
    if (strlen(latest_tag) == 0) {
        printf("Failed to resolve latest version from GitHub releases.\n");
        free(latest_tag);
        return;
    }

    printf("Current version: %s\n", BUILD_VERSION);
    printf("Latest version:  %s\n", latest_tag);
    bool up_to_date = str_equal(BUILD_VERSION, latest_tag);
    if (up_to_date && !force_update) {
        printf("Nature is up to date.\n");
        free(latest_tag);
        return;
    }

    if (check_only) {
        if (up_to_date) {
            printf("Nature is up to date.\n");
        } else {
            printf("Update available.\n");
        }
        free(latest_tag);
        return;
    }

    if (up_to_date && force_update) {
        printf("Force update enabled, reinstalling latest version.\n");
    }

    char *os = host_os_string();
    char *arch = host_arch_string();
    if (!os || !arch) {
        printf("Unsupported host platform for self-update.\n");
        free(latest_tag);
        return;
    }

    char *asset_url = fetch_latest_release_asset_url(os, arch);
    if (!asset_url) {
        printf("Failed to resolve release asset url for %s-%s.\n", os, arch);
        free(latest_tag);
        return;
    }

    trim_line_endings(asset_url);
    if (strlen(asset_url) == 0) {
        printf("No release asset found for %s-%s.\n", os, arch);
        free(latest_tag);
        free(asset_url);
        return;
    }

    char *install_root = detect_install_root(argv[0]);
    printf("Install root:     %s\n", install_root);
    printf("Release asset:    %s\n", asset_url);

    int status = apply_self_update(install_root, asset_url, auto_yes);
    free(latest_tag);
    free(asset_url);
    free(install_root);

    if (status != 0) {
        exit(1);
    }
}

#endif //NATURE_CMD_SELF_UPDATE_H
