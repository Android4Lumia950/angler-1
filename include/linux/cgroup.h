#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H
/*
 *  cgroup interface
 *
 *  Copyright (C) 2003 BULL SA
 *  Copyright (C) 2004-2006 Silicon Graphics, Inc.
 *
 */

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/rculist.h>
#include <linux/cgroupstats.h>
#include <linux/fs.h>
#include <linux/percpu-refcount.h>

#ifdef CONFIG_CGROUPS

/*
 * All weight knobs on the default hierarhcy should use the following min,
 * default and max values.  The default value is the logarithmic center of
 * MIN and MAX and allows 100x to be expressed in both directions.
 */
#define CGROUP_WEIGHT_MIN		1
#define CGROUP_WEIGHT_DFL		100
#define CGROUP_WEIGHT_MAX		10000

/* a css_task_iter should be treated as an opaque object */
struct css_task_iter {
	struct cgroup_subsys		*ss;

	struct list_head		*cset_pos;
	struct list_head		*cset_head;

	struct list_head		*task_pos;
	struct list_head		*tasks_head;
	struct list_head		*mg_tasks_head;

	struct css_set			*cur_cset;
	struct task_struct		*cur_task;
	struct list_head		iters_node;	/* css_set->task_iters */
};

extern struct cgroup_root cgrp_dfl_root;
extern struct css_set init_css_set;

#define SUBSYS(_x) extern struct cgroup_subsys _x ## _cgrp_subsys;
#include <linux/cgroup_subsys.h>
#undef SUBSYS

#define SUBSYS(_x)								\
	extern struct static_key_true _x ## _cgrp_subsys_enabled_key;		\
	extern struct static_key_true _x ## _cgrp_subsys_on_dfl_key;
#include <linux/cgroup_subsys.h>
#undef SUBSYS

	/* reference count - access via css_[try]get() and css_put() */
	struct percpu_ref refcnt;

bool css_has_online_children(struct cgroup_subsys_state *css);
struct cgroup_subsys_state *css_from_id(int id, struct cgroup_subsys *ss);
struct cgroup_subsys_state *cgroup_get_e_css(struct cgroup *cgroup,
					     struct cgroup_subsys *ss);
struct cgroup_subsys_state *css_tryget_online_from_dir(struct dentry *dentry,
						       struct cgroup_subsys *ss);

struct cgroup *cgroup_get_from_path(const char *path);
struct cgroup *cgroup_get_from_fd(int fd);

int cgroup_attach_task_all(struct task_struct *from, struct task_struct *);
int cgroup_transfer_tasks(struct cgroup *to, struct cgroup *from);

/**
 * css_get - obtain a reference on the specified css
 * @css: target css
 *
 * The caller must already have a reference.
 */
static inline void css_get(struct cgroup_subsys_state *css)
{
	/* We don't need to reference count the root state */
	if (!(css->flags & CSS_ROOT))
		percpu_ref_get(&css->refcnt);
}

/**
 * css_tryget - try to obtain a reference on the specified css
 * @css: target css
 *
 * Obtain a reference on @css if it's alive.  The caller naturally needs to
 * ensure that @css is accessible but doesn't have to be holding a
 * reference on it - IOW, RCU protected access is good enough for this
 * function.  Returns %true if a reference count was successfully obtained;
 * %false otherwise.
 */
static inline bool css_tryget(struct cgroup_subsys_state *css)
{
	if (css->flags & CSS_ROOT)
		return true;
	return percpu_ref_tryget(&css->refcnt);
}

/**
 * css_put - put a css reference
 * @css: target css
 *
 * Put a reference obtained via css_get() and css_tryget().
 */
static inline void css_put(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_ROOT))
		percpu_ref_put(&css->refcnt);
}

/* bits in struct cgroup flags field */
enum {
	/* Control Group is dead */
	CGRP_DEAD,
	/*
	 * Control Group has previously had a child cgroup or a task,
	 * but no longer (only if CGRP_NOTIFY_ON_RELEASE is set)
	 */
	CGRP_RELEASABLE,
	/* Control Group requires release notifications to userspace */
	CGRP_NOTIFY_ON_RELEASE,
	/*
	 * Clone the parent's configuration when creating a new child
	 * cpuset cgroup.  For historical reasons, this option can be
	 * specified at mount time and thus is implemented here.
	 */
	CGRP_CPUSET_CLONE_CHILDREN,
	/* see the comment above CGRP_ROOT_SANE_BEHAVIOR for details */
	CGRP_SANE_BEHAVIOR,
};

struct cgroup_name {
	struct rcu_head rcu_head;
	char name[];
};

struct cgroup {
	unsigned long flags;		/* "unsigned long" so bitops work */

	int id;				/* idr allocated in-hierarchy ID */

	/*
	 * We link our 'sibling' struct into our parent's 'children'.
	 * Our children link their 'sibling' into our 'children'.
	 */
	struct list_head sibling;	/* my parent's children */
	struct list_head children;	/* my children */
	struct list_head files;		/* my files */

	struct cgroup *parent;		/* my parent */
	struct dentry *dentry;		/* cgroup fs entry, RCU protected */

	/*
	 * Monotonically increasing unique serial number which defines a
	 * uniform order among all cgroups.  It's guaranteed that all
	 * ->children lists are in the ascending order of ->serial_nr.
	 * It's used to allow interrupting and resuming iterations.
	 */
	u64 serial_nr;

	/*
	 * This is a copy of dentry->d_name, and it's needed because
	 * we can't use dentry->d_name in cgroup_path().
	 *
	 * You must acquire rcu_read_lock() to access cgrp->name, and
	 * the only place that can change it is rename(), which is
	 * protected by parent dir's i_mutex.
	 *
	 * Normally you should use cgroup_name() wrapper rather than
	 * access it directly.
	 */
	struct cgroup_name __rcu *name;

	/* Private pointers for each registered subsystem */
	struct cgroup_subsys_state *subsys[CGROUP_SUBSYS_COUNT];

	struct cgroupfs_root *root;

	/*
	 * List of cgrp_cset_links pointing at css_sets with tasks in this
	 * cgroup.  Protected by css_set_lock.
	 */
	struct list_head cset_links;

	/*
	 * Linked list running through all cgroups that can
	 * potentially be reaped by the release agent. Protected by
	 * release_list_lock
	 */
	struct list_head release_list;

	/*
	 * list of pidlists, up to two for each namespace (one for procs, one
	 * for tasks); created on demand.
	 */
	struct list_head pidlists;
	struct mutex pidlist_mutex;

	/* For css percpu_ref killing and RCU-protected deletion */
	struct rcu_head rcu_head;
	struct work_struct destroy_work;
	atomic_t css_kill_cnt;

	/* List of events which userspace want to receive */
	struct list_head event_list;
	spinlock_t event_list_lock;

	/* directory xattrs */
	struct simple_xattrs xattrs;
};

#define MAX_CGROUP_ROOT_NAMELEN 64

/* cgroupfs_root->flags */
enum {
	/*
	 * Unfortunately, cgroup core and various controllers are riddled
	 * with idiosyncrasies and pointless options.  The following flag,
	 * when set, will force sane behavior - some options are forced on,
	 * others are disallowed, and some controllers will change their
	 * hierarchical or other behaviors.
	 *
	 * The set of behaviors affected by this flag are still being
	 * determined and developed and the mount option for this flag is
	 * prefixed with __DEVEL__.  The prefix will be dropped once we
	 * reach the point where all behaviors are compatible with the
	 * planned unified hierarchy, which will automatically turn on this
	 * flag.
	 *
	 * The followings are the behaviors currently affected this flag.
	 *
	 * - Mount options "noprefix" and "clone_children" are disallowed.
	 *   Also, cgroupfs file cgroup.clone_children is not created.
	 *
	 * - When mounting an existing superblock, mount options should
	 *   match.
	 *
	 * - Remount is disallowed.
	 *
	 * - cpuset: tasks will be kept in empty cpusets when hotplug happens
	 *   and take masks of ancestors with non-empty cpus/mems, instead of
	 *   being moved to an ancestor.
	 *
	 * - cpuset: a task can be moved into an empty cpuset, and again it
	 *   takes masks of ancestors.
	 *
	 * - "tasks" is removed.  Everything should be at process
	 *   granularity.  Use "cgroup.procs" instead.
	 *
	 * - "release_agent" and "notify_on_release" are removed.
	 *   Replacement notification mechanism will be implemented.
	 *
	 * - rename(2) is disallowed.
	 *
	 * - memcg: use_hierarchy is on by default and the cgroup file for
	 *   the flag is not created.
	 */
	CGRP_ROOT_SANE_BEHAVIOR	= (1 << 0),

	CGRP_ROOT_NOPREFIX	= (1 << 1), /* mounted subsystems have no named prefix */
	CGRP_ROOT_XATTR		= (1 << 2), /* supports extended attributes */

	/* mount options live below bit 16 */
	CGRP_ROOT_OPTION_MASK	= (1 << 16) - 1,

	CGRP_ROOT_SUBSYS_BOUND	= (1 << 16), /* subsystems finished binding */
};

/*
 * A cgroupfs_root represents the root of a cgroup hierarchy, and may be
 * associated with a superblock to form an active hierarchy.  This is
 * internal to cgroup core.  Don't access directly from controllers.
 */
struct cgroupfs_root {
	struct super_block *sb;

	/* The bitmask of subsystems attached to this hierarchy */
	unsigned long subsys_mask;

	/* Unique id for this hierarchy. */
	int hierarchy_id;

	/* A list running through the attached subsystems */
	struct list_head subsys_list;

	/* The root cgroup for this hierarchy */
	struct cgroup top_cgroup;

	/* Tracks how many cgroups are currently defined in hierarchy.*/
	int number_of_cgroups;

	/* A list running through the active hierarchies */
	struct list_head root_list;

	/* Hierarchy-specific flags */
	unsigned long flags;

	/* IDs for cgroups in this hierarchy */
	struct idr cgroup_idr;

	/* The path to use for release notifications. */
	char release_agent_path[PATH_MAX];

	/* The name for this hierarchy - may be empty */
	char name[MAX_CGROUP_ROOT_NAMELEN];
};

/*
 * A css_set is a structure holding pointers to a set of
 * cgroup_subsys_state objects. This saves space in the task struct
 * object and speeds up fork()/exit(), since a single inc/dec and a
 * list_add()/del() can bump the reference count on the entire cgroup
 * set for a task.
 */

struct css_set {

	/* Reference count */
	atomic_t refcount;

	/*
	 * List running through all cgroup groups in the same hash
	 * slot. Protected by css_set_lock
	 */
	struct hlist_node hlist;

	/*
	 * List running through all tasks using this cgroup
	 * group. Protected by css_set_lock
	 */
	struct list_head tasks;

	/*
	 * List of cgrp_cset_links pointing at cgroups referenced from this
	 * css_set.  Protected by css_set_lock.
	 */
	struct list_head cgrp_links;

	/*
	 * Set of subsystem states, one for each subsystem. This array
	 * is immutable after creation apart from the init_css_set
	 * during subsystem registration (at boot time) and modular subsystem
	 * loading/unloading.
	 */
	struct cgroup_subsys_state *subsys[CGROUP_SUBSYS_COUNT];

	/* For RCU-protected deletion */
	struct rcu_head rcu_head;
};

/*
 * cgroup_map_cb is an abstract callback API for reporting map-valued
 * control files
 */

struct cgroup_map_cb {
	int (*fill)(struct cgroup_map_cb *cb, const char *key, u64 value);
	void *state;
};

/*
 * struct cftype: handler definitions for cgroup control files
 *
 * Walk @parent's children.  Must be called under rcu_read_lock().
 *
 * If a subsystem synchronizes ->css_online() and the start of iteration, a
 * css which finished ->css_online() is guaranteed to be visible in the
 * future iterations and will stay visible until the last reference is put.
 * A css which hasn't finished ->css_online() or already finished
 * ->css_offline() may show up during traversal.  It's each subsystem's
 * responsibility to synchronize against on/offlining.
 *
 * It is allowed to temporarily drop RCU read lock during iteration.  The
 * caller is responsible for ensuring that @pos remains accessible until
 * the start of the next iteration by, for example, bumping the css refcnt.
 */
#define css_for_each_child(pos, parent)					\
	for ((pos) = css_next_child(NULL, (parent)); (pos);		\
	     (pos) = css_next_child((pos), (parent)))

/* cftype->flags */
enum {
	CFTYPE_ONLY_ON_ROOT	= (1 << 0),	/* only create on root cgrp */
	CFTYPE_NOT_ON_ROOT	= (1 << 1),	/* don't create on root cgrp */
	CFTYPE_INSANE		= (1 << 2),	/* don't create if sane_behavior */
};

#define MAX_CFTYPE_NAME		64

struct cftype {
	/*
	 * By convention, the name should begin with the name of the
	 * subsystem, followed by a period.  Zero length string indicates
	 * end of cftype array.
	 */
	char name[MAX_CFTYPE_NAME];
	int private;
	/*
	 * If not 0, file mode is set to this value, otherwise it will
	 * be figured out automatically
	 */
	umode_t mode;

	/*
	 * If non-zero, defines the maximum length of string that can
	 * be passed to write_string; defaults to 64
	 */
	size_t max_write_len;

	/* CFTYPE_* flags */
	unsigned int flags;

	int (*open)(struct inode *inode, struct file *file);
	ssize_t (*read)(struct cgroup *cgrp, struct cftype *cft,
			struct file *file,
			char __user *buf, size_t nbytes, loff_t *ppos);
	/*
	 * read_u64() is a shortcut for the common case of returning a
	 * single integer. Use it in place of read()
	 */
	u64 (*read_u64)(struct cgroup *cgrp, struct cftype *cft);
	/*
	 * read_s64() is a signed version of read_u64()
	 */
	s64 (*read_s64)(struct cgroup *cgrp, struct cftype *cft);
	/*
	 * read_map() is used for defining a map of key/value
	 * pairs. It should call cb->fill(cb, key, value) for each
	 * entry. The key/value pairs (and their ordering) should not
	 * change between reboots.
	 */
	int (*read_map)(struct cgroup *cgrp, struct cftype *cft,
			struct cgroup_map_cb *cb);
	/*
	 * read_seq_string() is used for outputting a simple sequence
	 * using seqfile.
	 */
	int (*read_seq_string)(struct cgroup *cgrp, struct cftype *cft,
			       struct seq_file *m);

	ssize_t (*write)(struct cgroup *cgrp, struct cftype *cft,
			 struct file *file,
			 const char __user *buf, size_t nbytes, loff_t *ppos);

	/*
	 * write_u64() is a shortcut for the common case of accepting
	 * a single integer (as parsed by simple_strtoull) from
	 * userspace. Use in place of write(); return 0 or error.
	 */
	int (*write_u64)(struct cgroup *cgrp, struct cftype *cft, u64 val);
	/*
	 * write_s64() is a signed version of write_u64()
	 */
	int (*write_s64)(struct cgroup *cgrp, struct cftype *cft, s64 val);

	/*
	 * write_string() is passed a nul-terminated kernelspace
	 * buffer of maximum length determined by max_write_len.
	 * Returns 0 or -ve error code.
	 */
	int (*write_string)(struct cgroup *cgrp, struct cftype *cft,
			    const char *buffer);
	/*
	 * trigger() callback can be used to get some kick from the
	 * userspace, when the actual string written is not important
	 * at all. The private field can be used to determine the
	 * kick type for multiplexing.
	 */
	int (*trigger)(struct cgroup *cgrp, unsigned int event);

	int (*release)(struct inode *inode, struct file *file);

	/*
	 * register_event() callback will be used to add new userspace
	 * waiter for changes related to the cftype. Implement it if
	 * you want to provide this functionality. Use eventfd_signal()
	 * on eventfd to send notification to userspace.
	 */
	int (*register_event)(struct cgroup *cgrp, struct cftype *cft,
			struct eventfd_ctx *eventfd, const char *args);
	/*
	 * unregister_event() callback will be called when userspace
	 * closes the eventfd or on cgroup removing.
	 * This callback must be implemented, if you want provide
	 * notification functionality.
	 */
	void (*unregister_event)(struct cgroup *cgrp, struct cftype *cft,
			struct eventfd_ctx *eventfd);
};

/*
 * cftype_sets describe cftypes belonging to a subsystem and are chained at
 * cgroup_subsys->cftsets.  Each cftset points to an array of cftypes
 * terminated by zero length name.
 */
#define css_for_each_descendant_pre(pos, css)				\
	for ((pos) = css_next_descendant_pre(NULL, (css)); (pos);	\
	     (pos) = css_next_descendant_pre((pos), (css)))

struct cgroup_scanner {
	struct cgroup *cgrp;
	int (*test_task)(struct task_struct *p, struct cgroup_scanner *scan);
	void (*process_task)(struct task_struct *p,
			struct cgroup_scanner *scan);
	struct ptr_heap *heap;
	void *data;
};

/*
 * See the comment above CGRP_ROOT_SANE_BEHAVIOR for details.  This
 * function can be called as long as @cgrp is accessible.
 */
static inline bool cgroup_sane_behavior(const struct cgroup *cgrp)
{
	return cgrp->root->flags & CGRP_ROOT_SANE_BEHAVIOR;
}

/* Caller should hold rcu_read_lock() */
static inline const char *cgroup_name(const struct cgroup *cgrp)
{
	return rcu_dereference(cgrp->name)->name;
}

int cgroup_add_cftypes(struct cgroup_subsys *ss, struct cftype *cfts);
int cgroup_rm_cftypes(struct cgroup_subsys *ss, struct cftype *cfts);

bool cgroup_is_descendant(struct cgroup *cgrp, struct cgroup *ancestor);

int cgroup_path(const struct cgroup *cgrp, char *buf, int buflen);
int task_cgroup_path(struct task_struct *task, char *buf, size_t buflen);

int cgroup_task_count(const struct cgroup *cgrp);

/*
 * Control Group taskset, used to pass around set of tasks to cgroup_subsys
 * methods.
 */
struct cgroup_taskset;
struct task_struct *cgroup_taskset_first(struct cgroup_taskset *tset);
struct task_struct *cgroup_taskset_next(struct cgroup_taskset *tset);
struct cgroup *cgroup_taskset_cur_cgroup(struct cgroup_taskset *tset);
int cgroup_taskset_size(struct cgroup_taskset *tset);

/**
 * cgroup_taskset_for_each - iterate cgroup_taskset
 * @task: the loop cursor
 * @dst_css: the destination css
 * @tset: taskset to iterate
 *
 * @tset may contain multiple tasks and they may belong to multiple
 * processes.
 *
 * On the v2 hierarchy, there may be tasks from multiple processes and they
 * may not share the source or destination csses.
 *
 * On traditional hierarchies, when there are multiple tasks in @tset, if a
 * task of a process is in @tset, all tasks of the process are in @tset.
 * Also, all are guaranteed to share the same source and destination csses.
 *
 * Iteration is not in any specific order.
 */
#define cgroup_taskset_for_each(task, dst_css, tset)			\
	for ((task) = cgroup_taskset_first((tset), &(dst_css));		\
	     (task);							\
	     (task) = cgroup_taskset_next((tset), &(dst_css)))

/**
 * cgroup_taskset_for_each_leader - iterate group leaders in a cgroup_taskset
 * @leader: the loop cursor
 * @dst_css: the destination css
 * @tset: takset to iterate
 *
 * Iterate threadgroup leaders of @tset.  For single-task migrations, @tset
 * may not contain any.
 */
#define cgroup_taskset_for_each_leader(leader, dst_css, tset)		\
	for ((leader) = cgroup_taskset_first((tset), &(dst_css));	\
	     (leader);							\
	     (leader) = cgroup_taskset_next((tset), &(dst_css)))	\
		if ((leader) != (leader)->group_leader)			\
			;						\
		else

/*
 * Inline functions.
 */

struct cgroup_subsys {
	struct cgroup_subsys_state *(*css_alloc)(struct cgroup *cgrp);
	int (*css_online)(struct cgroup *cgrp);
	void (*css_offline)(struct cgroup *cgrp);
	void (*css_free)(struct cgroup *cgrp);

	int (*can_attach)(struct cgroup *cgrp, struct cgroup_taskset *tset);
	void (*cancel_attach)(struct cgroup *cgrp, struct cgroup_taskset *tset);
	void (*attach)(struct cgroup *cgrp, struct cgroup_taskset *tset);
	void (*fork)(struct task_struct *task);
	void (*exit)(struct cgroup *cgrp, struct cgroup *old_cgrp,
		     struct task_struct *task);
	void (*bind)(struct cgroup *root);

	int subsys_id;
	int disabled;
	int early_init;
	/*
	 * True if this subsys uses ID. ID is not available before cgroup_init()
	 * (not available in early_init time.)
	 */
	bool use_id;

	/*
	 * If %false, this subsystem is properly hierarchical -
	 * configuration, resource accounting and restriction on a parent
	 * cgroup cover those of its children.  If %true, hierarchy support
	 * is broken in some ways - some subsystems ignore hierarchy
	 * completely while others are only implemented half-way.
	 *
	 * It's now disallowed to create nested cgroups if the subsystem is
	 * broken and cgroup core will emit a warning message on such
	 * cases.  Eventually, all subsystems will be made properly
	 * hierarchical and this will go away.
	 */
	bool broken_hierarchy;
	bool warned_broken_hierarchy;

#define MAX_CGROUP_TYPE_NAMELEN 32
	const char *name;

	/*
	 * Link to parent, and list entry in parent's children.
	 * Protected by cgroup_lock()
	 */
	struct cgroupfs_root *root;
	struct list_head sibling;
	/* used when use_id == true */
	struct idr idr;
	spinlock_t id_lock;

	/* list of cftype_sets */
	struct list_head cftsets;

	/* base cftypes, automatically [de]registered with subsys itself */
	struct cftype *base_cftypes;
	struct cftype_set base_cftset;

	/* should be defined only by modular subsystems */
	struct module *module;
};

#define SUBSYS(_x) extern struct cgroup_subsys _x ## _subsys;
#define IS_SUBSYS_ENABLED(option) IS_BUILTIN(option)
#include <linux/cgroup_subsys.h>
#undef IS_SUBSYS_ENABLED
#undef SUBSYS

/**
 * cgroup_css - obtain a cgroup's css for the specified subsystem
 * @cgrp: the cgroup of interest
 * @subsys_id: the subsystem of interest
 *
 * Return @cgrp's css (cgroup_subsys_state) associated with @subsys_id.
 */
static inline struct cgroup_subsys_state *cgroup_css(struct cgroup *cgrp,
						     int subsys_id)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_get(&css->refcnt);
}

/**
 * css_tryget - try to obtain a reference on the specified css
 * @css: target css
 *
 * Obtain a reference on @css unless it already has reached zero and is
 * being released.  This function doesn't care whether @css is on or
 * offline.  The caller naturally needs to ensure that @css is accessible
 * but doesn't have to be holding a reference on it - IOW, RCU protected
 * access is good enough for this function.  Returns %true if a reference
 * count was successfully obtained; %false otherwise.
 */
static inline bool css_tryget(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		return percpu_ref_tryget(&css->refcnt);
	return true;
}

/**
 * css_tryget_online - try to obtain a reference on the specified css if online
 * @css: target css
 *
 * Obtain a reference on @css if it's online.  The caller naturally needs
 * to ensure that @css is accessible but doesn't have to be holding a
 * reference on it - IOW, RCU protected access is good enough for this
 * function.  Returns %true if a reference count was successfully obtained;
 * %false otherwise.
 */
static inline bool css_tryget_online(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		return percpu_ref_tryget_live(&css->refcnt);
	return true;
}

/**
 * css_put - put a css reference
 * @css: target css
 *
 * Put a reference obtained via css_get() and css_tryget_online().
 */
static inline void css_put(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_put(&css->refcnt);
}

static inline void cgroup_get(struct cgroup *cgrp)
{
	css_get(&cgrp->self);
}

static inline bool cgroup_tryget(struct cgroup *cgrp)
{
	return css_tryget(&cgrp->self);
}

static inline void cgroup_put(struct cgroup *cgrp)
{
	css_put(&cgrp->self);
}

/**
 * task_css_set_check - obtain a task's css_set with extra access conditions
 * @task: the task to obtain css_set for
 * @__c: extra condition expression to be passed to rcu_dereference_check()
 *
 * A task's css_set is RCU protected, initialized and exited while holding
 * task_lock(), and can only be modified while holding both cgroup_mutex
 * and task_lock() while the task is alive.  This macro verifies that the
 * caller is inside proper critical section and returns @task's css_set.
 *
 * The caller can also specify additional allowed conditions via @__c, such
 * as locks used during the cgroup_subsys::attach() methods.
 */
#ifdef CONFIG_PROVE_RCU
extern struct mutex cgroup_mutex;
extern spinlock_t css_set_lock;
#define task_css_set_check(task, __c)					\
	rcu_dereference_check((task)->cgroups,				\
		lockdep_is_held(&cgroup_mutex) ||			\
		lockdep_is_held(&css_set_lock) ||			\
		((task)->flags & PF_EXITING) || (__c))
#else
#define task_css_set_check(task, __c)					\
	rcu_dereference((task)->cgroups)
#endif

/**
 * task_css_check - obtain css for (task, subsys) w/ extra access conds
 * @task: the target task
 * @subsys_id: the target subsystem ID
 * @__c: extra condition expression to be passed to rcu_dereference_check()
 *
 * Return the cgroup_subsys_state for the (@task, @subsys_id) pair.  The
 * synchronization rules are the same as task_css_set_check().
 */
#define task_css_check(task, subsys_id, __c)				\
	task_css_set_check((task), (__c))->subsys[(subsys_id)]

/**
 * task_css_set - obtain a task's css_set
 * @task: the task to obtain css_set for
 *
 * See task_css_set_check().
 */
static inline struct css_set *task_css_set(struct task_struct *task)
{
	return task_css_set_check(task, false);
}

/**
 * task_css - obtain css for (task, subsys)
 * @task: the target task
 * @subsys_id: the target subsystem ID
 *
 * See task_css_check().
 */
static inline struct cgroup_subsys_state *task_css(struct task_struct *task,
						   int subsys_id)
{
	return task_css_check(task, subsys_id, false);
}

static inline struct cgroup *task_cgroup(struct task_struct *task,
					 int subsys_id)
{
	return task_css(task, subsys_id)->cgroup;
}

struct cgroup *cgroup_next_sibling(struct cgroup *pos);

/**
 * cgroup_for_each_child - iterate through children of a cgroup
 * @pos: the cgroup * to use as the loop cursor
 * @cgrp: cgroup whose children to walk
 *
 * Walk @cgrp's children.  Must be called under rcu_read_lock().  A child
 * cgroup which hasn't finished ->css_online() or already has finished
 * ->css_offline() may show up during traversal and it's each subsystem's
 * responsibility to verify that each @pos is alive.
 *
 * If a subsystem synchronizes against the parent in its ->css_online() and
 * before starting iterating, a cgroup which finished ->css_online() is
 * guaranteed to be visible in the future iterations.
 *
 * It is allowed to temporarily drop RCU read lock during iteration.  The
 * caller is responsible for ensuring that @pos remains accessible until
 * the start of the next iteration by, for example, bumping the css refcnt.
 */
#define cgroup_for_each_child(pos, cgrp)				\
	for ((pos) = list_first_or_null_rcu(&(cgrp)->children,		\
					    struct cgroup, sibling);	\
	     (pos); (pos) = cgroup_next_sibling((pos)))

static inline struct cgroup *task_cgroup(struct task_struct *task,
					 int subsys_id)
{
	return task_css(task, subsys_id)->cgroup;
}

static inline struct cgroup *task_dfl_cgroup(struct task_struct *task)
{
	return task_css_set(task)->dfl_cgrp;
}

static inline struct cgroup *cgroup_parent(struct cgroup *cgrp)
{
	struct cgroup_subsys_state *parent_css = cgrp->self.parent;

	if (parent_css)
		return container_of(parent_css, struct cgroup, self);
	return NULL;
}

/**
 * cgroup_on_dfl - test whether a cgroup is on the default hierarchy
 * @cgrp: the cgroup of interest
 *
 * The default hierarchy is the v2 interface of cgroup and this function
 * can be used to test whether a cgroup is on the default hierarchy for
 * cases where a subsystem should behave differnetly depending on the
 * interface version.
 *
 * The set of behaviors which change on the default hierarchy are still
 * being determined and the mount option is prefixed with __DEVEL__.
 *
 * List of changed behaviors:
 *
 * - Mount options "noprefix", "xattr", "clone_children", "release_agent"
 *   and "name" are disallowed.
 *
 * - When mounting an existing superblock, mount options should match.
 *
 * - Remount is disallowed.
 *
 * - rename(2) is disallowed.
 *
 * - "tasks" is removed.  Everything should be at process granularity.  Use
 *   "cgroup.procs" instead.
 *
 * Alternatively, a subsystem may choose to use a single global lock to
 * synchronize ->css_online() and ->css_offline() against tree-walking
 * operations.
 *
 * It is allowed to temporarily drop RCU read lock during iteration.  The
 * caller is responsible for ensuring that @pos remains accessible until
 * the start of the next iteration by, for example, bumping the css refcnt.
 */
static inline bool cgroup_on_dfl(const struct cgroup *cgrp)
{
	return cgrp->root == &cgrp_dfl_root;
}
/*
 * cgroup_is_descendant - test ancestry
 * @cgrp: the cgroup to be tested
 * @ancestor: possible ancestor of @cgrp
 *
 * Test whether @cgrp is a descendant of @ancestor.  It also returns %true
 * if @cgrp == @ancestor.  This function is safe to call as long as @cgrp
 * and @ancestor are accessible.
 */
static inline bool cgroup_is_descendant(struct cgroup *cgrp,
					struct cgroup *ancestor)
{
	if (cgrp->root != ancestor->root || cgrp->level < ancestor->level)
		return false;
	return cgrp->ancestor_ids[ancestor->level] == ancestor->id;
}

/**
 * task_under_cgroup_hierarchy - test task's membership of cgroup ancestry
 * @task: the task to be tested
 * @ancestor: possible ancestor of @task's cgroup
 *
 * Tests whether @task's default cgroup hierarchy is a descendant of @ancestor.
 * It follows all the same rules as cgroup_is_descendant, and only applies
 * to the default hierarchy.
 */
static inline bool task_under_cgroup_hierarchy(struct task_struct *task,
					       struct cgroup *ancestor)
{
	struct css_set *cset = task_css_set(task);

/* A cgroup_iter should be treated as an opaque object */
struct cgroup_iter {
	struct list_head *cset_link;
	struct list_head *task;
};

/*
 * Name / path handling functions.  All are thin wrappers around the kernfs
 * counterparts and can be called under any context.
 */

static inline int cgroup_name(struct cgroup *cgrp, char *buf, size_t buflen)
{
	return kernfs_name(cgrp->kn, buf, buflen);
}

static inline int cgroup_path(struct cgroup *cgrp, char *buf, size_t buflen)
{

	return kernfs_path(cgrp->kn, buf, buflen);
}

static inline void pr_cont_cgroup_name(struct cgroup *cgrp)
{
	pr_cont_kernfs_name(cgrp->kn);
}

/* Get id and depth of css */
unsigned short css_id(struct cgroup_subsys_state *css);
struct cgroup_subsys_state *cgroup_css_from_dir(struct file *f, int id);

#else /* !CONFIG_CGROUPS */

struct cgroup_subsys_state;
struct cgroup;

static inline void css_put(struct cgroup_subsys_state *css) {}
static inline int cgroup_attach_task_all(struct task_struct *from,
					 struct task_struct *t) { return 0; }
static inline int cgroupstats_build(struct cgroupstats *stats,
				    struct dentry *dentry) { return -EINVAL; }

static inline void cgroup_fork(struct task_struct *p) {}
static inline int cgroup_can_fork(struct task_struct *p) { return 0; }
static inline void cgroup_cancel_fork(struct task_struct *p) {}
static inline void cgroup_post_fork(struct task_struct *p) {}
static inline void cgroup_exit(struct task_struct *p) {}
static inline void cgroup_free(struct task_struct *p) {}

static inline int cgroup_init_early(void) { return 0; }
static inline int cgroup_init(void) { return 0; }

static inline bool task_under_cgroup_hierarchy(struct task_struct *task,
					       struct cgroup *ancestor)
{
	return true;
}

static inline void cgroup_init_kthreadd(void) {}
static inline void cgroup_kthread_ready(void) {}

static inline int subsys_cgroup_allow_attach(struct cgroup_subsys_state *css,
					     struct cgroup_taskset *tset)
{
	return 0;
}

#endif /* !CONFIG_CGROUPS */

/*
 * sock->sk_cgrp_data handling.  For more info, see sock_cgroup_data
 * definition in cgroup-defs.h.
 */
#ifdef CONFIG_SOCK_CGROUP_DATA

#if defined(CONFIG_CGROUP_NET_PRIO) || defined(CONFIG_CGROUP_NET_CLASSID)
extern spinlock_t cgroup_sk_update_lock;
#endif

void cgroup_sk_alloc_disable(void);
void cgroup_sk_alloc(struct sock_cgroup_data *skcd);
void cgroup_sk_clone(struct sock_cgroup_data *skcd);
void cgroup_sk_free(struct sock_cgroup_data *skcd);

static inline struct cgroup *sock_cgroup_ptr(struct sock_cgroup_data *skcd)
{
#if defined(CONFIG_CGROUP_NET_PRIO) || defined(CONFIG_CGROUP_NET_CLASSID)
	unsigned long v;

	/*
	 * @skcd->val is 64bit but the following is safe on 32bit too as we
	 * just need the lower ulong to be written and read atomically.
	 */
	v = READ_ONCE(skcd->val);

	if (v & 3)
		return &cgrp_dfl_root.cgrp;

	return (struct cgroup *)(unsigned long)v ?: &cgrp_dfl_root.cgrp;
#else
	return (struct cgroup *)(unsigned long)skcd->val;
#endif
}

#else	/* CONFIG_CGROUP_DATA */

static inline void cgroup_sk_alloc(struct sock_cgroup_data *skcd) {}
static inline void cgroup_sk_clone(struct sock_cgroup_data *skcd) {}
static inline void cgroup_sk_free(struct sock_cgroup_data *skcd) {}

#endif	/* CONFIG_CGROUP_DATA */

struct cgroup_namespace {
	atomic_t		count;
	struct ns_common	ns;
	struct user_namespace	*user_ns;
	struct css_set          *root_cset;
};

extern struct cgroup_namespace init_cgroup_ns;

#ifdef CONFIG_CGROUPS

void free_cgroup_ns(struct cgroup_namespace *ns);

struct cgroup_namespace *copy_cgroup_ns(unsigned long flags,
					struct user_namespace *user_ns,
					struct cgroup_namespace *old_ns);

int cgroup_path_ns(struct cgroup *cgrp, char *buf, size_t buflen,
		   struct cgroup_namespace *ns);

#else /* !CONFIG_CGROUPS */

static inline void free_cgroup_ns(struct cgroup_namespace *ns) { }
static inline struct cgroup_namespace *
copy_cgroup_ns(unsigned long flags, struct user_namespace *user_ns,
	       struct cgroup_namespace *old_ns)
{
	return old_ns;
}

#endif /* !CONFIG_CGROUPS */

static inline void get_cgroup_ns(struct cgroup_namespace *ns)
{
	if (ns)
		atomic_inc(&ns->count);
}

static inline void put_cgroup_ns(struct cgroup_namespace *ns)
{
	if (ns && atomic_dec_and_test(&ns->count))
		free_cgroup_ns(ns);
}

#endif /* _LINUX_CGROUP_H */
