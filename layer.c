#include "private.h"

/* local functions */
bool
layer_fb_get(struct liftoff_rpi_layer *layer)
{
   struct liftoff_rpi_property *fb;

   fb = layer_property_get(layer, LIFTOFF_RPI_PROP_FB_ID);
   return (fb != NULL && fb->value != 0);
}

bool
layer_visible_get(struct liftoff_rpi_layer *layer)
{
   struct liftoff_rpi_property *prop;

   prop = layer_property_get(layer, LIFTOFF_RPI_PROP_ALPHA);
   if (prop && prop->value == 0)
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "Layer %p Alpha Prop Zero",
                        (void *)layer);
        return false;
     }

   if (layer->force_comp)
     return true;
   else
     {
        struct liftoff_rpi_property *fb;

        fb = layer_property_get(layer, LIFTOFF_RPI_PROP_FB_ID);
        return (fb != NULL && fb->value != 0);
     }
}

static void
layer_rect_get(struct liftoff_rpi_layer *layer, struct liftoff_rpi_rect *rect)
{
   struct liftoff_rpi_property *xprop, *yprop, *wprop, *hprop;

   xprop = layer_property_get(layer, LIFTOFF_RPI_PROP_CRTC_X);
   yprop = layer_property_get(layer, LIFTOFF_RPI_PROP_CRTC_Y);
   wprop = layer_property_get(layer, LIFTOFF_RPI_PROP_CRTC_W);
   hprop = layer_property_get(layer, LIFTOFF_RPI_PROP_CRTC_H);

   rect->x = (xprop != NULL ? xprop->value : 0);
   rect->y = (yprop != NULL ? yprop->value : 0);
   rect->w = (wprop != NULL ? wprop->value : 0);
   rect->h = (hprop != NULL ? hprop->value : 0);
}

bool
layer_intersects(struct liftoff_rpi_layer *a, struct liftoff_rpi_layer *b)
{
   struct liftoff_rpi_rect ra, rb;

   if (!layer_visible_get(a) || !layer_visible_get(b))
     return false;

   layer_rect_get(a, &ra);
   layer_rect_get(b, &rb);

   return ra.x < rb.x + rb.w && ra.y < rb.y + rb.h &&
     ra.x + ra.w > rb.x && ra.y + ra.h > rb.y;
}

void
layer_clean(struct liftoff_rpi_layer *layer)
{
   size_t i = 0;

   layer->changed = false;
   layer->prev_fb_info = layer->fb_info;
   for (; i < layer->props_len; i++)
     layer->props[i].prev_value = layer->props[i].value;
}

void
layer_priority_update(struct liftoff_rpi_layer *layer, bool current)
{
   struct liftoff_rpi_property *prop;

   prop = layer_property_get(layer, LIFTOFF_RPI_PROP_FB_ID);
   if (prop != NULL && prop->prev_value != prop->value)
     layer->pending_priority++;

   if (current)
     {
        if (layer->current_priority != layer->pending_priority)
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "Layer %p priority change: %d -> %d",
                             (void *)layer, layer->current_priority,
                             layer->pending_priority);
          }

        layer->current_priority = layer->pending_priority;
        layer->pending_priority = 0;
     }
}

struct liftoff_rpi_property *
layer_property_get(struct liftoff_rpi_layer *layer, int prop)
{
   size_t i = 0;

   for (; i < layer->props_len; i++)
     {
        if (layer->props[i].index == prop)
          return &layer->props[i];
     }

   return NULL;
}

void
layer_candidate_plane_add(struct liftoff_rpi_layer *layer, struct liftoff_rpi_plane *plane)
{
   size_t i = 0;
   ssize_t empty = -1;
   char *type = NULL;

   for (; i < layer->output->dev->planes_cap; i++)
     {
        if (layer->candidate_planes[i] == plane->id)
          return;
        if (empty < 0 && layer->candidate_planes[i] == 0)
          empty = (ssize_t)i;
     }

   switch (plane->type)
     {
      case DRM_PLANE_TYPE_PRIMARY:
        type = "PRIMARY";
        break;
      case DRM_PLANE_TYPE_CURSOR:
        type = "CURSOR";
        break;
      case DRM_PLANE_TYPE_OVERLAY:
        type = "OVERLAY";
        break;
      default:
        break;
     }

   if (empty < 0) return;

   layer->candidate_planes[empty] = plane->id;
}

void
layer_candidate_planes_reset(struct liftoff_rpi_layer *layer)
{
   memset(layer->candidate_planes, 0,
          sizeof(layer->candidate_planes[0]) * layer->output->dev->planes_cap);
}

int
layer_cache_fb_info(struct liftoff_rpi_layer *layer)
{
   struct liftoff_rpi_property *fb_id_prop;
   drmModeFB2 *fb_info;
   size_t i = 0, j = 0, num_planes;
   int ret;

   fb_id_prop = layer_property_get(layer, LIFTOFF_RPI_PROP_FB_ID);
   if (!fb_id_prop || fb_id_prop->value == 0)
     {
        memset(&layer->fb_info, 0, sizeof(layer->fb_info));
        return 0;
     }

   if (layer->fb_info.fb_id == fb_id_prop->value)
     return 0;

   fb_info = drmModeGetFB2(layer->output->dev->fd, fb_id_prop->value);
   if (!fb_info)
     {
        if (errno == EINVAL)
          return 0;
        return -errno;
     }

   /* drmModeGetFB2() always creates new GEM handles -- close these, we
    * won't use them and we don't want to leak them */
   num_planes = sizeof(fb_info->handles) / sizeof(fb_info->handles[0]);
   for (i = 0; i < num_planes; i++)
     {
        if (fb_info->handles[i] == 0)
          continue;

        ret = drmCloseBufferHandle(layer->output->dev->fd,
                                   fb_info->handles[i]);
        if (ret != 0)
          {
             liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "drmCloseBufferHandle");
             continue;
          }

        /* Make sure we don't double-close a handle */
        for (j = i + 1; j < num_planes; j++)
          {
             if (fb_info->handles[j] == fb_info->handles[i])
               fb_info->handles[j] = 0;
          }
        fb_info->handles[i] = 0;
     }

   layer->fb_info = *fb_info;
   drmModeFreeFB2(fb_info);
   return 0;
}

/* API functions */
struct liftoff_rpi_layer *
liftoff_rpi_layer_create(struct liftoff_rpi_output *output)
{
   struct liftoff_rpi_layer *layer;

   layer = calloc(1, sizeof(*layer));
   if (!layer)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "calloc");
        return NULL;
     }

   layer->output = output;

   layer->candidate_planes = calloc(sizeof(layer->candidate_planes[0]),
                                    output->dev->planes_cap);
   if (!layer->candidate_planes)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "calloc");
        free(layer);
        return NULL;
     }

   liftoff_rpi_list_insert(output->layers.prev, &layer->link);
   output->layers_changed = true;
   return layer;
}

void
liftoff_rpi_layer_destroy(struct liftoff_rpi_layer *layer)
{
   if (!layer) return;

   layer->output->layers_changed = true;
   if (layer->plane)
     layer->plane->layer = NULL;
   if (layer->output->comp_layer == layer)
     layer->output->comp_layer = NULL;
   free(layer->props);
   free(layer->candidate_planes);
   liftoff_rpi_list_remove(&layer->link);
   free(layer);
}

bool
liftoff_rpi_layer_needs_composition(struct liftoff_rpi_layer *layer)
{
   if (!layer_visible_get(layer)) return false;
   return (layer->plane == NULL);
}

int
liftoff_rpi_layer_property_set(struct liftoff_rpi_layer *layer, int property, uint64_t value)
{
   struct liftoff_rpi_property *prop, *props;

   if (property == LIFTOFF_RPI_PROP_CRTC_ID)
     {
        liftoff_rpi_log(LIFTOFF_RPI_ERROR, "refusing to set a layer's CRTC_ID");
        return -EINVAL;
     }

   prop = layer_property_get(layer, property);
   if (!prop)
     {
        props = realloc(layer->props, (layer->props_len + 1) *
                        sizeof(struct liftoff_rpi_property));
        if (!props)
          {
             liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "realloc");
             return -ENOMEM;
          }

        layer->props = props;
        layer->props_len++;

        prop = &layer->props[layer->props_len - 1];
        memset(prop, 0, sizeof(*prop));
        prop->index = property;

        layer->changed = true;
     }

   prop->value = value;

   if (property == LIFTOFF_RPI_PROP_FB_ID && layer->force_comp)
     {
        layer->force_comp = false;
        layer->changed = true;
     }

   return 0;
}

void
liftoff_rpi_layer_property_unset(struct liftoff_rpi_layer *layer, int property)
{
   struct liftoff_rpi_property *prop, *last;

   prop = layer_property_get(layer, property);
   if (!prop) return;

   last = &layer->props[layer->props_len - 1];
   if (prop != last)
     *prop = *last;

   memset(last, 0, sizeof(*last));
   layer->props_len--;

   layer->changed = true;
}

void
liftoff_rpi_layer_fb_composited_set(struct liftoff_rpi_layer *layer)
{
   if (layer->force_comp) return;

   liftoff_rpi_layer_property_set(layer, LIFTOFF_RPI_PROP_FB_ID, 0);

   layer->force_comp = true;
   layer->changed = true;
}

struct liftoff_rpi_plane *
liftoff_rpi_layer_plane_get(struct liftoff_rpi_layer *layer)
{
   return layer->plane;
}

bool
liftoff_rpi_layer_candidate_plane_get(struct liftoff_rpi_layer *layer, struct liftoff_rpi_plane *plane)
{
   size_t i = 0;

   for (; i < layer->output->dev->planes_cap; i++)
     {
        if (layer->candidate_planes[i] == plane->id)
          return true;
     }

   return false;
}
