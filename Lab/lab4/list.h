// Circular Doubly Linked List
// Header-only Library (only list.h no list.c)
#ifndef LIST_H // 如果還沒定義過 LIST_H
#define LIST_H // 就定義 LIST_H

#include <stddef.h> // 為了拿到 size_t 和 offsetof

struct list_head {
    struct list_head *next, *prev;
};

// statement expression
// cast a member of a structure out to the containing structure
#define container_of(ptr, type, member) ({                      \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})
// The macro offsetof() returns the offset of the field member 
// from the start of the structure type.

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

/* 安全遍歷巨集：支援在迴圈中 list_del 節點 */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

// https://elixir.bootlin.com/linux/v6.0/source/include/linux/list.h 
// line 519

static inline void list_init (struct list_head *ptr) {
    ptr -> next = ptr;
    ptr -> prev = ptr;
};

// helper function
static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next) {
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/* 4. API: 插入到頭部 (常用於 stack 邏輯) */
// head -> new
static inline void list_add(struct list_head *new, struct list_head *head) {
    __list_add(new, head, head->next);
}

/* 5. API: 插入到尾部 (常用於 queue 邏輯) */
// new -> head
static inline void list_add_tail(struct list_head *new, struct list_head *head) {
    __list_add(new, head->prev, head);
}

/* 6. API: 刪除節點 (O(1) 的關鍵) */
static inline void list_del(struct list_head *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    // 建議刪除後把指標清空，避免意外存取
    entry->next = entry->prev = 0; 
}

/* 7. 判斷是否為空 */
static inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

#endif // LIST_H 結束
