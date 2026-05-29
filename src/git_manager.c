/* git_manager.c — Abstraction layer over libgit2-glib */

#include "git_manager.h"
#include <libgit2-glib/ggit.h>

/* ------------------------------------------------------------------ */
/* Global State                                                         */
/* ------------------------------------------------------------------ */

static GgitRepository *repo_instance = NULL;
static GObject        *signal_hub    = NULL;

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
gboolean git_manager_unstage_file (const gchar *path) { return FALSE; /* TODO */ }
gboolean git_manager_stage_all (void) { return FALSE; /* TODO */ }
gboolean git_manager_discard_file (const gchar *path) { return FALSE; /* TODO */ }
gboolean git_manager_commit (const gchar *message) {
    if (!repo_instance || !message) return FALSE;
    GError *error = NULL;
    GgitIndex *index = ggit_repository_get_index (repo_instance, &error);
    if (!index) return FALSE;
    GgitOId *tree_id = ggit_index_write_tree (index, &error);
    g_object_unref (index);
    if (!tree_id) return FALSE;
    GgitTree *tree = ggit_repository_lookup_tree (repo_instance, tree_id, &error);
    g_object_unref (tree_id);
    if (!tree) return FALSE;
    
    GgitSignature *sig = ggit_signature_new_now ("Aether User", "user@vcodex.ide", &error);
    GgitRef *head = ggit_repository_get_head (repo_instance, NULL);
    GgitCommit *parent = NULL;
    if (head) {
        GgitOId *target = ggit_ref_get_target (head);
        parent = ggit_repository_lookup_commit (repo_instance, target, NULL);
        g_object_unref (head);
    }
    
    GgitCommit *parents[1] = { parent };
    GgitOId *commit_id = ggit_repository_create_commit (repo_instance, "HEAD", sig, sig, "UTF-8", message, tree, parent ? parents : NULL, parent ? 1 : 0, &error);
    
    if (parent) g_object_unref (parent);
    g_object_unref (tree);
    g_object_unref (sig);
    if (commit_id) {
        g_object_unref (commit_id);
        emit_status_changed();
        return TRUE;
    }
    return FALSE;
}
gboolean git_manager_commit_all (const gchar *message) { return FALSE; /* TODO */ }

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

gboolean git_manager_fetch (void) { return FALSE; }
gboolean git_manager_pull (void) { return FALSE; }
gboolean git_manager_push (void) { return FALSE; }
gint     git_manager_get_ahead (void) { return 0; }
gint     git_manager_get_behind (void) { return 0; }

/* ------------------------------------------------------------------ */
/* Diff & Log                                                           */
/* ------------------------------------------------------------------ */

GPtrArray *git_manager_diff_file (const gchar *path) { return NULL; /* TODO */ }
void       git_hunk_free (GitHunk *hunk) { if(hunk) { g_free(hunk->content); g_free(hunk); } }
GPtrArray *git_manager_get_log (guint max_count) { return NULL; /* TODO */ }
gchar     *git_manager_get_commit_diff (const gchar *sha) { return NULL; /* TODO */ }
void       vcodex_git_commit_free (GitCommit *commit) {
    if (!commit) return;
    g_free(commit->sha); g_free(commit->short_sha);
    g_free(commit->author); g_free(commit->email); g_free(commit->message);
    g_free(commit);
}
