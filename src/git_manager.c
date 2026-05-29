/* git_manager.c — Abstraction layer over libgit2-glib */

#include "git_manager.h"
#include <libgit2-glib/ggit.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Global State                                                         */
/* ------------------------------------------------------------------ */

static GgitRepository *repo_instance = NULL;
static GObject        *signal_hub    = NULL;
static gchar          *workspace_dir_path = NULL;

/* We use a dummy GObject class to emit "status-changed" signal */
#define TYPE_GIT_SIGNAL_HUB (git_signal_hub_get_type ())
G_DECLARE_FINAL_TYPE (GitSignalHub, git_signal_hub, GIT, SIGNAL_HUB, GObject)

struct _GitSignalHub {
    GObject parent_instance;
};

G_DEFINE_TYPE (GitSignalHub, git_signal_hub, G_TYPE_OBJECT)

enum {
    SIGNAL_STATUS_CHANGED,
    LAST_SIGNAL
};
static guint hub_signals[LAST_SIGNAL] = { 0 };

static void
git_signal_hub_class_init (GitSignalHubClass *klass)
{
    hub_signals[SIGNAL_STATUS_CHANGED] =
        g_signal_new ("status-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
git_signal_hub_init (GitSignalHub *self)
{
}

/* ------------------------------------------------------------------ */
/* Helper: Emit update                                                  */
/* ------------------------------------------------------------------ */

static void
emit_status_changed (void)
{
    if (signal_hub) {
        g_signal_emit (signal_hub, hub_signals[SIGNAL_STATUS_CHANGED], 0);
    }
}

/* ------------------------------------------------------------------ */
/* Core Lifecycle                                                     */
/* ------------------------------------------------------------------ */

gboolean
git_manager_init (const gchar *workspace_dir)
{
    /* Initialize libgit2 */
    ggit_init ();

    if (!signal_hub)
        signal_hub = g_object_new (TYPE_GIT_SIGNAL_HUB, NULL);

    if (repo_instance) {
        g_object_unref (repo_instance);
        repo_instance = NULL;
    }

    if (!workspace_dir) return FALSE;

    g_free(workspace_dir_path);
    workspace_dir_path = g_strdup(workspace_dir);

    GFile *location = g_file_new_for_path (workspace_dir);
    GError *error = NULL;

    /* ggit_repository_open_ext might be needed to search up, but let's try direct first */
    repo_instance = ggit_repository_open (location, &error);
    
    /* If direct open fails, try to discover repository path */
    if (!repo_instance) {
        g_clear_error (&error);
        GFile *repo_file = ggit_repository_discover_full (location, TRUE, NULL, &error);
        if (repo_file) {
            repo_instance = ggit_repository_open (repo_file, &error);
            g_object_unref (repo_file);
        }
    }
    
    g_object_unref (location);

    if (error) {
        g_printerr ("Git Init Error: %s\n", error->message);
        g_error_free (error);
        return FALSE;
    }

    emit_status_changed ();
    return TRUE;
}

void
git_manager_shutdown (void)
{
    if (repo_instance) {
        g_object_unref (repo_instance);
        repo_instance = NULL;
    }
    if (signal_hub) {
        g_object_unref (signal_hub);
        signal_hub = NULL;
    }
}

gboolean
git_manager_is_repo (void)
{
    return repo_instance != NULL;
}

GObject *
git_manager_get_instance (void)
{
    return signal_hub;
}

/* ------------------------------------------------------------------ */
/* Status Retrieval                                                   */
/* ------------------------------------------------------------------ */

void
git_file_entry_free (GitFileEntry *entry)
{
    if (!entry) return;
    g_free (entry->path);
    g_free (entry);
}

static gint
map_ggit_status_to_git_file_status (GgitStatusFlags flags)
{
    gint st = 0;
    
    if (flags & GGIT_STATUS_INDEX_NEW)        st |= GIT_FILE_STATUS_STAGED;
    if (flags & GGIT_STATUS_INDEX_MODIFIED)   st |= GIT_FILE_STATUS_STAGED;
    if (flags & GGIT_STATUS_INDEX_DELETED)    st |= GIT_FILE_STATUS_STAGED | GIT_FILE_STATUS_DELETED;
    if (flags & GGIT_STATUS_INDEX_RENAMED)    st |= GIT_FILE_STATUS_STAGED | GIT_FILE_STATUS_RENAMED;
    
    if (flags & GGIT_STATUS_WORKING_TREE_MODIFIED) st |= GIT_FILE_STATUS_MODIFIED;
    if (flags & GGIT_STATUS_WORKING_TREE_DELETED)  st |= GIT_FILE_STATUS_DELETED;
    if (flags & GGIT_STATUS_WORKING_TREE_NEW)      st |= GIT_FILE_STATUS_UNTRACKED;
    if (flags & GGIT_STATUS_WORKING_TREE_RENAMED)  st |= GIT_FILE_STATUS_RENAMED;
    
    if (flags & GGIT_STATUS_CONFLICTED)            st |= GIT_FILE_STATUS_CONFLICTED;
    
    return st;
}

static gint
status_foreach_cb (const gchar *path, GgitStatusFlags status_flags, gpointer user_data)
{
    GPtrArray *array = (GPtrArray *) user_data;
    
    if (status_flags == GGIT_STATUS_CURRENT || status_flags == GGIT_STATUS_IGNORED)
        return 0;

    GitFileEntry *entry = g_new0 (GitFileEntry, 1);
    entry->path   = g_strdup (path);
    entry->status = map_ggit_status_to_git_file_status (status_flags);
    
    g_ptr_array_add (array, entry);
    return 0;
}

GPtrArray *
git_manager_get_status (void)
{
    if (!repo_instance) return NULL;

    GPtrArray *array = g_ptr_array_new_with_free_func ((GDestroyNotify) git_file_entry_free);
    GError *error = NULL;
    
    GgitStatusOptions *opts = ggit_status_options_new (
        GGIT_STATUS_OPTION_INCLUDE_UNTRACKED | 
        GGIT_STATUS_OPTION_RECURSE_UNTRACKED_DIRS, 
        GGIT_STATUS_SHOW_INDEX_AND_WORKDIR,
        NULL);
        
    ggit_repository_file_status_foreach (repo_instance, opts, status_foreach_cb, array, &error);
    
    if (opts) ggit_status_options_free (opts);

    if (error) {
        g_printerr ("Git Status Error: %s\n", error->message);
        g_error_free (error);
        g_ptr_array_free (array, TRUE);
        return NULL;
    }

    return array;
}

/* ------------------------------------------------------------------ */
/* Staging & Committing                                                 */
/* ------------------------------------------------------------------ */

gboolean git_manager_stage_file (const gchar *path) {
    if (!repo_instance) return FALSE;
    GError *error = NULL;
    GgitIndex *index = ggit_repository_get_index (repo_instance, &error);
    if (!index) return FALSE;
    gboolean ok = ggit_index_add_path (index, path, &error);
    if (ok) ok = ggit_index_write (index, &error);
    g_object_unref (index);
    if (ok) emit_status_changed ();
    return ok;
}
gboolean git_manager_unstage_file (const gchar *path) {
    if (!workspace_dir_path || !path) return FALSE;
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *quoted_path = g_shell_quote(path);
    gchar *cmd = g_strdup_printf("git -C %s reset HEAD -- %s", quoted_ws, quoted_path);
    g_free(quoted_ws);
    g_free(quoted_path);
    
    gint status;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL);
    g_free(cmd);
    if (res && status == 0) {
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}

gboolean git_manager_stage_all (void) {
    if (!workspace_dir_path) return FALSE;
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *cmd = g_strdup_printf("git -C %s add .", quoted_ws);
    g_free(quoted_ws);
    
    gint status;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL);
    g_free(cmd);
    if (res && status == 0) {
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}

gboolean git_manager_discard_file (const gchar *path) {
    if (!workspace_dir_path || !path) return FALSE;
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *quoted_path = g_shell_quote(path);
    gchar *cmd = g_strdup_printf("git -C %s checkout -- %s", quoted_ws, quoted_path);
    g_free(quoted_ws);
    g_free(quoted_path);
    
    gint status;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL);
    g_free(cmd);
    if (res && status == 0) {
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}
gboolean git_manager_commit (const gchar *message) {
    if (!workspace_dir_path || !message) return FALSE;
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *quoted_msg = g_shell_quote(message);
    gchar *cmd = g_strdup_printf("git -C %s commit -m %s", quoted_ws, quoted_msg);
    g_free(quoted_ws);
    g_free(quoted_msg);
    
    gint status;
    gchar *err_out = NULL;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, &err_out, &status, NULL);
    g_free(cmd);
    g_free(err_out);
    
    if (res && status == 0) {
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}

gboolean git_manager_commit_all (const gchar *message) {
    if (!workspace_dir_path || !message) return FALSE;
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *quoted_msg = g_shell_quote(message);
    gchar *cmd = g_strdup_printf("git -C %s commit -a -m %s", quoted_ws, quoted_msg);
    g_free(quoted_ws);
    g_free(quoted_msg);
    
    gint status;
    gchar *err_out = NULL;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, &err_out, &status, NULL);
    g_free(cmd);
    g_free(err_out);
    
    if (res && status == 0) {
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Branching                                                          */
/* ------------------------------------------------------------------ */

gchar *git_manager_get_current_branch (void)
{
    if (!repo_instance) return NULL;
    GError *error = NULL;
    GgitRef *head = ggit_repository_get_head (repo_instance, &error);
    if (!head) {
        if (error) g_error_free (error);
        return NULL;
    }
    
    gchar *name = NULL;
    if (ggit_ref_is_branch (head)) {
        name = g_strdup (ggit_ref_get_shorthand (head));
    }
    g_object_unref (head);
    return name;
}

GPtrArray *git_manager_list_branches (void) { return NULL; /* TODO */ }
gboolean   git_manager_checkout_branch (const gchar *name) { return FALSE; /* TODO */ }
gboolean   git_manager_create_branch (const gchar *name) { return FALSE; /* TODO */ }

/* ------------------------------------------------------------------ */
/* Remote                                                             */
/* ------------------------------------------------------------------ */

gboolean git_manager_fetch (void) {
    if (!workspace_dir_path) return FALSE;
    gchar *quoted = g_shell_quote(workspace_dir_path);
    gchar *cmd = g_strdup_printf("git -C %s fetch", quoted);
    g_free(quoted);
    
    gchar *err_out = NULL;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, &err_out, NULL, NULL);
    g_free(cmd);
    g_free(err_out);
    return res;
}
gboolean git_manager_pull (void) {
    if (!workspace_dir_path) return FALSE;
    gchar *quoted = g_shell_quote(workspace_dir_path);
    gchar *cmd = g_strdup_printf("git -C %s pull", quoted);
    g_free(quoted);
    
    gint status;
    gchar *err_out = NULL;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, &err_out, &status, NULL);
    g_free(cmd);
    g_free(err_out);
    if (res && status == 0) {
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}
gboolean git_manager_push (void) {
    if (!workspace_dir_path) return FALSE;
    gchar *quoted = g_shell_quote(workspace_dir_path);
    gchar *cmd = g_strdup_printf("git -C %s push", quoted);
    g_free(quoted);
    
    gint status;
    gchar *err_out = NULL;
    gboolean res = g_spawn_command_line_sync(cmd, NULL, &err_out, &status, NULL);
    g_free(cmd);
    g_free(err_out);
    if (res && status == 0) {
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}
gint git_manager_get_ahead (void) {
    if (!workspace_dir_path) return 0;
    gchar *quoted = g_shell_quote(workspace_dir_path);
    gchar *cmd = g_strdup_printf("git -C %s rev-list --count @{u}..HEAD", quoted);
    g_free(quoted);
    
    gchar *out = NULL;
    gchar *err_out = NULL;
    g_spawn_command_line_sync(cmd, &out, &err_out, NULL, NULL);
    g_free(cmd);
    g_free(err_out);
    
    gint ahead = 0;
    if (out) { ahead = atoi(out); g_free(out); }
    return ahead;
}
gint git_manager_get_behind (void) {
    if (!workspace_dir_path) return 0;
    gchar *quoted = g_shell_quote(workspace_dir_path);
    gchar *cmd = g_strdup_printf("git -C %s rev-list --count HEAD..@{u}", quoted);
    g_free(quoted);
    
    gchar *out = NULL;
    gchar *err_out = NULL;
    g_spawn_command_line_sync(cmd, &out, &err_out, NULL, NULL);
    g_free(cmd);
    g_free(err_out);
    
    gint behind = 0;
    if (out) { behind = atoi(out); g_free(out); }
    return behind;
}

/* ------------------------------------------------------------------ */
/* Diff & Log                                                           */
/* ------------------------------------------------------------------ */

GPtrArray *git_manager_diff_file (const gchar *path) { 
    if (!workspace_dir_path || !path) return NULL;
    
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *quoted_path = g_shell_quote(path);
    gchar *cmd = g_strdup_printf("git -C %s diff -U0 -- %s", quoted_ws, quoted_path);
    g_free(quoted_ws);
    g_free(quoted_path);
    
    gchar *out = NULL;
    gchar *err_out = NULL;
    g_spawn_command_line_sync(cmd, &out, &err_out, NULL, NULL);
    g_free(cmd);
    g_free(err_out);
    
    if (!out) return NULL;
    
    GPtrArray *hunks = g_ptr_array_new_with_free_func((GDestroyNotify)git_hunk_free);
    gchar **lines = g_strsplit(out, "\n", -1);
    g_free(out);
    
    for (gint i = 0; lines[i]; i++) {
        if (g_str_has_prefix(lines[i], "@@ ")) {
            GitHunk *h = g_new0(GitHunk, 1);
            /* Parse @@ -old_start,old_lines +new_start,new_lines @@ */
            sscanf(lines[i], "@@ -%d,%d +%d,%d", &h->old_start, &h->old_lines, &h->new_start, &h->new_lines);
            if (h->old_lines == 0) h->kind = GIT_HUNK_ADDED;
            else if (h->new_lines == 0) h->kind = GIT_HUNK_REMOVED;
            else h->kind = GIT_HUNK_MODIFIED;
            g_ptr_array_add(hunks, h);
        }
    }
    g_strfreev(lines);
    return hunks;
}
void       git_hunk_free (GitHunk *hunk) { if(hunk) { g_free(hunk->content); g_free(hunk); } }
GPtrArray *git_manager_get_log (guint max_count) {
    if (!workspace_dir_path) return NULL;
    
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *cmd = g_strdup_printf("git -C %s log -n %u --pretty=format:\"%%H|%%h|%%an|%%ae|%%at|%%s\"", quoted_ws, max_count);
    g_free(quoted_ws);
    
    gchar *out = NULL;
    gchar *err_out = NULL;
    g_spawn_command_line_sync(cmd, &out, &err_out, NULL, NULL);
    g_free(cmd);
    g_free(err_out);
    if (!out) return NULL;
    
    GPtrArray *log = g_ptr_array_new_with_free_func((GDestroyNotify)vcodex_git_commit_free);
    gchar **lines = g_strsplit(out, "\n", -1);
    g_free(out);
    
    for (gint i = 0; lines[i]; i++) {
        gchar **parts = g_strsplit(lines[i], "|", 6);
        if (g_strv_length(parts) == 6) {
            GitCommit *c = g_new0(GitCommit, 1);
            c->sha = g_strdup(parts[0]);
            c->short_sha = g_strdup(parts[1]);
            c->author = g_strdup(parts[2]);
            c->email = g_strdup(parts[3]);
            c->timestamp = g_ascii_strtoll(parts[4], NULL, 10);
            c->message = g_strdup(parts[5]);
            g_ptr_array_add(log, c);
        }
        g_strfreev(parts);
    }
    g_strfreev(lines);
    return log;
}
gchar     *git_manager_get_commit_diff (const gchar *sha) {
    if (!workspace_dir_path || !sha) return NULL;
    
    gchar *quoted_ws = g_shell_quote(workspace_dir_path);
    gchar *quoted_sha = g_shell_quote(sha);
    gchar *cmd = g_strdup_printf("git -C %s show %s", quoted_ws, quoted_sha);
    g_free(quoted_ws);
    g_free(quoted_sha);
    
    gchar *out = NULL;
    gchar *err_out = NULL;
    g_spawn_command_line_sync(cmd, &out, &err_out, NULL, NULL);
    g_free(cmd);
    g_free(err_out);
    return out;
}
void       vcodex_git_commit_free (GitCommit *commit) {
    if (!commit) return;
    g_free(commit->sha); g_free(commit->short_sha);
    g_free(commit->author); g_free(commit->email); g_free(commit->message);
    g_free(commit);
}
