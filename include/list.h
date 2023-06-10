#ifndef LIST_H
# define LIST_H

# include <stdbool.h>
# include <stddef.h>

# define liftoff_rpi_container_of(ptr, sample, member)			\
	(__typeof__(sample))((char *)(ptr) -				\
			     offsetof(__typeof__(*sample), member))

# define liftoff_rpi_list_for_each(pos, head, member)			\
	for (pos = liftoff_rpi_container_of((head)->next, pos, member);	\
	     &pos->member != (head);					\
	     pos = liftoff_rpi_container_of(pos->member.next, pos, member))

# define liftoff_rpi_list_for_each_safe(pos, tmp, head, member)		\
	for (pos = liftoff_rpi_container_of((head)->next, pos, member),	\
	     tmp = liftoff_rpi_container_of(pos->member.next, tmp, member); \
	     &pos->member != (head);					\
	     pos = tmp,							\
	     tmp = liftoff_rpi_container_of(pos->member.next, tmp, member))

struct liftoff_rpi_list
{
   struct liftoff_rpi_list *prev;
   struct liftoff_rpi_list *next;
};

void liftoff_rpi_list_init(struct liftoff_rpi_list *list);
void liftoff_rpi_list_insert(struct liftoff_rpi_list *list, struct liftoff_rpi_list *elm);
void liftoff_rpi_list_remove(struct liftoff_rpi_list *elm);
size_t liftoff_rpi_list_length(const struct liftoff_rpi_list *list);
bool liftoff_rpi_list_empty(const struct liftoff_rpi_list *list);

#endif
