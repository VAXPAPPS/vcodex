#ifndef GIT_MANAGER_H
#define GIT_MANAGER_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Core Lifecycle                                                     */
/* ------------------------------------------------------------------ */

/**
 * git_manager_init:
 * Initializes libgit2 and tries to open the repository at @workspace_dir.
 * Returns TRUE if a repository was successfully opened.
 */
gboolean git_manager_init (const gchar *workspace_dir);

/**
 * git_manager_shutdown:
 * Cleans up libgit2 resources.
 */
void git_manager_shutdown (void);

/**
 * git_manager_is_repo:
 * Returns TRUE if the current workspace is a valid Git repository.
 */
gboolean git_manager_is_repo (void);

/* ------------------------------------------------------------------ */
/* Signals and Singleton                                                */
/* ------------------------------------------------------------------ */

/* The singleton instance to listen for "status-changed" signal */
GObject *git_manager_get_instance (void);

/* ------------------------------------------------------------------ */
/* Status Retrieval                                                   */
/* ------------------------------------------------------------------ */

typedef enum {
    GIT_FILE_STATUS_CURRENT    = 0,
    GIT_FILE_STATUS_MODIFIED   = 1 << 0, /* modified in worktree */
    GIT_FILE_STATUS_STAGED     = 1 << 1, /* added to index */
    GIT_FILE_STATUS_UNTRACKED  = 1 << 2, /* untracked */
    GIT_FILE_STATUS_DELETED    = 1 << 3, /* deleted */
    GIT_FILE_STATUS_RENAMED    = 1 << 4, /* renamed */
    GIT_FILE_STATUS_CONFLICTED = 1 << 5, /* conflicted/unmerged */
} GitFileStatus;

typedef struct {
    gchar        *path;        /* relative path from repo root */
    GitFileStatus status;
} GitFileEntry;

/**
 * git_manager_get_status:
 * Returns a GPtrArray of GitFileEntry*, or NULL if error/not a repo.
 */
GPtrArray *git_manager_get_status (void);

void git_file_entry_free (GitFileEntry *entry);

/* ------------------------------------------------------------------ */
/* Staging & Committing                                                 */
/* ------------------------------------------------------------------ */

gboolean git_manager_stage_file   (const gchar *path);
gboolean git_manager_unstage_file (const gchar *path);
gboolean git_manager_stage_all    (void);
gboolean git_manager_discard_file (const gchar *path);

gboolean git_manager_commit       (const gchar *message);
gboolean git_manager_commit_all   (const gchar *message);

/* ------------------------------------------------------------------ */
/* Branching                                                          */
/* ------------------------------------------------------------------ */

gchar     *git_manager_get_current_branch (void); /* caller frees */
GPtrArray *git_manager_list_branches      (void); /* GPtrArray of gchar* */
gboolean   git_manager_checkout_branch    (const gchar *name);
gboolean   git_manager_create_branch      (const gchar *name);

/* ------------------------------------------------------------------ */
/* Remote (Push / Pull)                                               */
/* ------------------------------------------------------------------ */

gboolean git_manager_fetch      (void);
gboolean git_manager_pull       (void);
gboolean git_manager_push       (void);
gint     git_manager_get_ahead  (void);
gint     git_manager_get_behind (void);

/* ------------------------------------------------------------------ */
/* Diff (for Gutter & Log)                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    GIT_HUNK_ADDED,
    GIT_HUNK_REMOVED,
    GIT_HUNK_MODIFIED
} GitHunkKind;

typedef struct {
    gint        old_start;
    gint        old_lines;
    gint        new_start;
    gint        new_lines;
    gchar      *content;
    GitHunkKind kind;
} GitHunk;

/**
 * git_manager_diff_file:
 * Gets the diff hunks for a file (worktree vs index).
 */
GPtrArray *git_manager_diff_file (const gchar *path);
void       git_hunk_free         (GitHunk *hunk);

/* ------------------------------------------------------------------ */
/* Commit History (Log)                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    gchar  *sha;
    gchar  *short_sha;
    gchar  *author;
    gchar  *email;
    gchar  *message;
    gint64  timestamp;
} GitCommit;

GPtrArray *git_manager_get_log         (guint max_count);
gchar     *git_manager_get_commit_diff (const gchar *sha);
void       vcodex_git_commit_free      (GitCommit *commit);

G_END_DECLS

#endif /* GIT_MANAGER_H */
