#include "list.h"

void
liftoff_rpi_list_init(struct liftoff_rpi_list *list)
{
   list->prev = list;
   list->next = list;
}

void
liftoff_rpi_list_insert(struct liftoff_rpi_list *list, struct liftoff_rpi_list *elm)
{
   elm->prev = list;
   elm->next = list->next;
   list->next = elm;
   elm->next->prev = elm;
}

void
liftoff_rpi_list_remove(struct liftoff_rpi_list *elm)
{
   elm->prev->next = elm->next;
   elm->next->prev = elm->prev;
   elm->next = NULL;
   elm->prev = NULL;
}

size_t
liftoff_rpi_list_length(const struct liftoff_rpi_list *list)
{
   struct liftoff_rpi_list *e;
   size_t count;

   count = 0;
   e = list->next;
   while (e != list)
     {
        e = e->next;
        count++;
     }

   return count;
}

bool
liftoff_rpi_list_empty(const struct liftoff_rpi_list *list)
{
   return list->next == list;
}
