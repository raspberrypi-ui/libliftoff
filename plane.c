#include "private.h"

/* local functions */
static int
plane_zpos_guess(struct liftoff_rpi_device *dev, uint32_t id, uint32_t type)
{
   struct liftoff_rpi_plane *primary;

   switch (type)
     {
      case DRM_PLANE_TYPE_PRIMARY:
        return 0;
      case DRM_PLANE_TYPE_CURSOR:
        return 2;
      case DRM_PLANE_TYPE_OVERLAY:
        if (liftoff_rpi_list_empty(&dev->planes)) return 0;
        primary = liftoff_rpi_container_of(dev->planes.next, primary, link);
        if (id < primary->id)
          return -1;
        else
          return 1;
     }

   return 0;
}

static int
plane_prop_range_check(const drmModePropertyRes *prop, uint64_t value)
{
   if (value < prop->values[0] || value > prop->values[1])
     return -EINVAL;

   return 0;
}

static int
plane_prop_signed_range_check(const drmModePropertyRes *prop, uint64_t value)
{
   if ((int64_t)value < (int64_t)prop->values[0] ||
       (int64_t)value > (int64_t)prop->values[1])
     return -EINVAL;

   return 0;
}

static int
plane_prop_enum_check(const drmModePropertyRes *prop, uint64_t value)
{
   int i = 0;

   for (; i < prop->count_enums; i++)
     {
        if (prop->enums[i].value == value)
          return 0;
     }

   return -EINVAL;
}

static int
plane_prop_bitmask_check(const drmModePropertyRes *prop, uint64_t value)
{
   int i = 0;
   uint64_t mask = 0;

   for (; i < prop->count_enums; i++)
     mask |= 1 << prop->enums[i].value;

   if ((value & ~mask) != 0)
     return -EINVAL;

   return 0;
}

static struct liftoff_rpi_property *
plane_property_get(struct liftoff_rpi_plane *plane, int prop)
{
   size_t i = 0;

   for (; i < plane->props_len; i++)
     {
        if (plane->props[i].index == prop)
          return &plane->props[i];
     }

   return NULL;
}

static int
plane_property_set(struct liftoff_rpi_plane *plane, drmModeAtomicReq *req, uint32_t id, uint64_t value)
{
   int ret = 0;

   ret = drmModeAtomicAddProperty(req, plane->id, id, value);
   if (ret < 0)
     {
        liftoff_rpi_log(LIFTOFF_RPI_ERROR, "drmModeAtomicAddProperty: %s",
                        strerror(-ret));
        return ret;
     }

   return 0;
}

static int
plane_prop_set(struct liftoff_rpi_plane *plane, drmModeAtomicReq *req, int index, uint64_t value)
{
   int ret = 0;
   struct liftoff_rpi_property *prop;

   prop = plane_property_get(plane, index);
   if (!prop)
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "plane %"PRIu32" is missing the %d property",
                        plane->id, index);
        return -EINVAL;
     }

   if (prop->dprop->flags & DRM_MODE_PROP_IMMUTABLE)
     return -EINVAL;

   switch (drmModeGetPropertyType(prop->dprop))
     {
      case DRM_MODE_PROP_RANGE:
        ret = plane_prop_range_check(prop->dprop, value);
        break;
      case DRM_MODE_PROP_ENUM:
        ret = plane_prop_enum_check(prop->dprop, value);
        break;
      case DRM_MODE_PROP_BITMASK:
        ret = plane_prop_bitmask_check(prop->dprop, value);
        break;
      case DRM_MODE_PROP_SIGNED_RANGE:
        ret = plane_prop_signed_range_check(prop->dprop, value);
        break;
     }

   if (ret != 0) return ret;

   return plane_property_set(plane, req, prop->id, value);
}

int
plane_apply(struct liftoff_rpi_plane *plane, struct liftoff_rpi_layer *layer, drmModeAtomicReq *req)
{
   int c, ret = 0;
   size_t i = 0;
   struct liftoff_rpi_property *lprop, *pprop;

   c = drmModeAtomicGetCursor(req);
   if (!layer)
     {
        ret = plane_prop_set(plane, req, LIFTOFF_RPI_PROP_FB_ID, 0);
        if (ret != 0) return ret;
        return plane_prop_set(plane, req, LIFTOFF_RPI_PROP_CRTC_ID, 0);
     }

   ret = plane_prop_set(plane, req, LIFTOFF_RPI_PROP_CRTC_ID, layer->output->crtc_id);
   if (ret != 0)
     {
        liftoff_rpi_log(LIFTOFF_RPI_ERROR, "Failed to set plane %d CRTC id", plane->id);
        return ret;
     }

   for (; i < layer->props_len; i++)
     {
        lprop = &layer->props[i];

        /* we don't support setting the zpos property. Only used
         * read-only during allocation */
        if (lprop->index == LIFTOFF_RPI_PROP_ZPOS)
          continue;

        pprop = plane_property_get(plane, lprop->index);
        if (!pprop)
          {
             if (lprop->index == LIFTOFF_RPI_PROP_ALPHA &&
                 lprop->value == 0xFFFF)
               continue;
             if (lprop->index == LIFTOFF_RPI_PROP_ROTATION &&
                 lprop->value == DRM_MODE_ROTATE_0)
               continue;
             if (lprop->index == LIFTOFF_RPI_PROP_SCALING_FILTER &&
                 lprop->value == 0)
               continue;
             if (lprop->index == LIFTOFF_RPI_PROP_PIXEL_BLEND_MODE &&
                 lprop->value == 0)
               continue;
             if (lprop->index == LIFTOFF_RPI_PROP_FB_DAMAGE_CLIPS)
               continue;

             drmModeAtomicSetCursor(req, c);
             return -EINVAL;
          }

        ret = plane_property_set(plane, req, pprop->id, lprop->value);
        if (ret != 0)
          {
             liftoff_rpi_log(LIFTOFF_RPI_ERROR,
                             "Failed to set Plane %d Property %d",
                             plane->id, pprop->id);
             drmModeAtomicSetCursor(req, c);
             return ret;
          }
     }

   return 0;
}

bool
plane_check_layer_fb(struct liftoff_rpi_plane *plane, struct liftoff_rpi_layer *layer)
{
   const struct drm_format_modifier_blob *set;
   const uint32_t *formats;
   const struct drm_format_modifier *modifiers;
   size_t i;
   ssize_t format_index, modifier_index;
   int format_shift;

   /* TODO: add support for legacy format list with implicit modifier */
   if (layer->fb_info.fb_id == 0 ||
       !(layer->fb_info.flags & DRM_MODE_FB_MODIFIERS) ||
       plane->in_formats_blob == NULL)
     return true; /* not enough information to reject */

   set = plane->in_formats_blob->data;

   formats = (void *)((char *)set + set->formats_offset);
   format_index = -1;
   for (i = 0; i < set->count_formats; ++i)
     {
      if (formats[i] == layer->fb_info.pixel_format)
          {
             format_index = (ssize_t)i;
             break;
          }
     }

   if (format_index < 0)
     return false;

   modifiers = (void *)((char *)set + set->modifiers_offset);
   modifier_index = -1;
   for (i = 0; i < set->count_modifiers; i++)
     {
        if (modifiers[i].modifier == layer->fb_info.modifier)
          {
             modifier_index = (ssize_t)i;
             break;
          }
     }

   if (modifier_index < 0)
     return false;

   if (format_index < (int)modifiers[modifier_index].offset ||
       format_index >= (int)modifiers[modifier_index].offset + 64)
     return false;

   format_shift = (int)(format_index - (int)modifiers[modifier_index].offset);
   return (modifiers[modifier_index].formats & ((uint64_t)1 << format_shift)) != 0;
}

/* API functions */
struct liftoff_rpi_plane *
liftoff_rpi_plane_create(struct liftoff_rpi_device *dev, uint32_t id)
{
   struct liftoff_rpi_plane *plane, *cur;
   struct liftoff_rpi_property *prop;
   drmModePlane *dplane;
   drmModeObjectProperties *dprops;
   drmModePropertyRes *dprop;
   uint32_t i = 0;
   bool has_type = false, has_zpos = false;

   liftoff_rpi_list_for_each(plane, &dev->planes, link)
     {
        if (plane->id == id)
          {
             liftoff_rpi_log(LIFTOFF_RPI_ERROR, "tried to register plane "
                             "%"PRIu32" twice\n", id);
             errno = EEXIST;
             return NULL;
          }
     }

   plane = calloc(1, sizeof(*plane));
   if (!plane)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "calloc");
        return NULL;
     }

   dplane = drmModeGetPlane(dev->fd, id);
   if (!dplane)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "drmModeGetPlane");
        free(plane);
        return NULL;
     }

   plane->id = dplane->plane_id;
   plane->possible_crtcs = dplane->possible_crtcs;
   drmModeFreePlane(dplane);

   dprops = drmModeObjectGetProperties(dev->fd, id, DRM_MODE_OBJECT_PLANE);
   if (!dprops)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "drmModeObjectGetProperties");
        free(plane);
        return NULL;
     }

   plane->props = calloc(dprops->count_props,
                         sizeof(struct liftoff_rpi_property));
   if (!plane->props)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "calloc");
        drmModeFreeObjectProperties(dprops);
        free(plane);
        return NULL;
     }

   for (; i < dprops->count_props; i++)
     {
        int x = -1;

        dprop = drmModeGetProperty(dev->fd, dprops->props[i]);
        if (!dprop)
          {
             liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "drmModeGetProperty");
             drmModeFreeObjectProperties(dprops);
             free(plane->props);
             free(plane);
             return NULL;
          }

        if (!strcmp(dprop->name, "type"))
          {
             x = LIFTOFF_RPI_PROP_TYPE;
             has_type = true;
             plane->type = dprops->prop_values[i];
          }
        else if (!strcmp(dprop->name, "FB_ID"))
          x = LIFTOFF_RPI_PROP_FB_ID;
        else if (!strcmp(dprop->name, "CRTC_ID"))
          x = LIFTOFF_RPI_PROP_CRTC_ID;
        else if (!strcmp(dprop->name, "CRTC_X"))
          x = LIFTOFF_RPI_PROP_CRTC_X;
        else if (!strcmp(dprop->name, "CRTC_Y"))
          x = LIFTOFF_RPI_PROP_CRTC_Y;
        else if (!strcmp(dprop->name, "CRTC_W"))
          x = LIFTOFF_RPI_PROP_CRTC_W;
        else if (!strcmp(dprop->name, "CRTC_H"))
          x = LIFTOFF_RPI_PROP_CRTC_H;
        else if (!strcmp(dprop->name, "SRC_X"))
          x = LIFTOFF_RPI_PROP_SRC_X;
        else if (!strcmp(dprop->name, "SRC_Y"))
          x = LIFTOFF_RPI_PROP_SRC_Y;
        else if (!strcmp(dprop->name, "SRC_W"))
          x = LIFTOFF_RPI_PROP_SRC_W;
        else if (!strcmp(dprop->name, "SRC_H"))
          x = LIFTOFF_RPI_PROP_SRC_H;
        else if (!strcmp(dprop->name, "zpos"))
          {
             x = LIFTOFF_RPI_PROP_ZPOS;
             has_zpos = true;
             plane->zpos = dprops->prop_values[i];
          }
        else if (!strcmp(dprop->name, "alpha"))
          x = LIFTOFF_RPI_PROP_ALPHA;
        else if (!strcmp(dprop->name, "rotation"))
          x = LIFTOFF_RPI_PROP_ROTATION;
        else if (!strcmp(dprop->name, "SCALING FILTER"))
          x = LIFTOFF_RPI_PROP_SCALING_FILTER;
        else if (!strcmp(dprop->name, "pixel blend mode"))
          x = LIFTOFF_RPI_PROP_PIXEL_BLEND_MODE;
        else if (!strcmp(dprop->name, "FB_DAMAGE_CLIPS"))
          x = LIFTOFF_RPI_PROP_FB_DAMAGE_CLIPS;
        else if (!strcmp(dprop->name, "IN_FENCE_FD"))
          x = LIFTOFF_RPI_PROP_IN_FENCE_FD;
        else if (!strcmp(dprop->name, "IN_FORMATS"))
          {
             x = LIFTOFF_RPI_PROP_IN_FORMATS;
             plane->in_formats_blob =
               drmModeGetPropertyBlob(dev->fd, dprops->prop_values[i]);
             if (!plane->in_formats_blob)
               {
                  liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR,
                                        "drmModeGetPropertyBlob");
                  drmModeFreeProperty(dprop);
                  drmModeFreeObjectProperties(dprops);
                  free(plane->props);
                  free(plane);
                  return NULL;
               }
          }

        if (x < 0)
          {
             drmModeFreeProperty(dprop);
             continue;
          }

        prop = &plane->props[i];
        prop->id = dprop->prop_id;
        prop->index = x;
        prop->dprop = dprop;

        plane->props_len++;
     }

   drmModeFreeObjectProperties(dprops);

   if (!has_type)
     {
        liftoff_rpi_log(LIFTOFF_RPI_ERROR,
                        "plane %"PRIu32" is missing the 'type' property",
                        plane->id);
        free(plane->props);
        free(plane);
        errno = EINVAL;
        return NULL;
     }
   else if (!has_zpos)
     plane->zpos = plane_zpos_guess(dev, plane->id, plane->type);

   if (plane->type == DRM_PLANE_TYPE_PRIMARY)
     liftoff_rpi_list_insert(&dev->planes, &plane->link);
   else
     {
        liftoff_rpi_list_for_each(cur, &dev->planes, link)
          {
             if (cur->type != DRM_PLANE_TYPE_PRIMARY &&
                 plane->zpos >= cur->zpos)
               {
                  liftoff_rpi_list_insert(cur->link.prev, &plane->link);
                  break;
               }
          }
        if (!plane->link.next)
          liftoff_rpi_list_insert(dev->planes.prev, &plane->link);
     }

   return plane;
}

void
liftoff_rpi_plane_destroy(struct liftoff_rpi_plane *plane)
{
   size_t i = 0;

   if (plane->layer) plane->layer->plane = NULL;
   liftoff_rpi_list_remove(&plane->link);

   for (; i < plane->props_len; i++)
     drmModeFreeProperty(plane->props[i].dprop);

   free(plane->props);
   drmModeFreePropertyBlob(plane->in_formats_blob);
   free(plane);
}

uint32_t
liftoff_rpi_plane_id_get(struct liftoff_rpi_plane *plane)
{
   return plane->id;
}

uint32_t
liftoff_rpi_plane_type_get(struct liftoff_rpi_plane *plane)
{
   return plane->type;
}
