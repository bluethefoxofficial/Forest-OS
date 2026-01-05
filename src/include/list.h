#ifndef LIST_H
#define LIST_H

#include <stddef.h>

// =============================================================================
// LINUX-STYLE DOUBLY-LINKED LIST IMPLEMENTATION
// =============================================================================
// Intrusive circular doubly-linked lists similar to Linux kernel
// =============================================================================

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
    struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

// Insert a new entry between two known consecutive entries
static inline void __list_add(struct list_head *new,
                             struct list_head *prev,
                             struct list_head *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

// Add a new entry after the specified head
static inline void list_add(struct list_head *new, struct list_head *head)
{
    __list_add(new, head, head->next);
}

// Add a new entry before the specified head (at tail)
static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
    __list_add(new, head->prev, head);
}

// Delete a list entry by making the prev/next entries point to each other
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

// Delete entry from list
static inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

// Delete entry and reinitialize it
static inline void list_del_init(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

// Replace old entry by new one
static inline void list_replace(struct list_head *old,
                               struct list_head *new)
{
    new->next = old->next;
    new->next->prev = new;
    new->prev = old->prev;
    new->prev->next = new;
}

// Move list from one head to another
static inline void list_move(struct list_head *list, struct list_head *head)
{
    __list_del(list->prev, list->next);
    list_add(list, head);
}

// Move list to tail of another list
static inline void list_move_tail(struct list_head *list,
                                 struct list_head *head)
{
    __list_del(list->prev, list->next);
    list_add_tail(list, head);
}

// Test whether a list is empty
static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

// Test whether a list has just one entry
static inline int list_is_singular(const struct list_head *head)
{
    return !list_empty(head) && (head->next == head->prev);
}

// Get the struct for this entry
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

// Get the first element from a list
#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

// Get the last element from a list  
#define list_last_entry(ptr, type, member) \
    list_entry((ptr)->prev, type, member)

// Iterate over a list
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

// Iterate over a list backwards
#define list_for_each_prev(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)

// Iterate over a list safe against removal
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

// Iterate over list of given type
#define list_for_each_entry(pos, head, member) \
    for (pos = list_entry((head)->next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_entry(pos->member.next, typeof(*pos), member))

// Iterate safely for the current entry
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_entry((head)->next, typeof(*pos), member), \
         n = list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = n, n = list_entry(n->member.next, typeof(*n), member))

// Container of macro - get containing structure
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); })

// offsetof is provided by stddef.h

#endif // LIST_H