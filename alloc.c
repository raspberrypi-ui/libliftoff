#include "private.h"

/* Plane allocation algorithm
 *
 * Goal: KMS exposes a set of hardware planes, user submitted a set of layers.
 * We want to map as many layers as possible to planes.
 *
 * However, all layers can't be mapped to any plane. There are constraints,
 * sometimes depending on driver-specific limitations or the configuration of
 * other planes.
 *
 * The only way to discover driver-specific limitations is via an atomic test
 * commit: we submit a plane configuration, and KMS replies whether it's
 * supported or not. Thus we need to incrementally build a valid configuration.
 *
 * Let's take an example with 2 planes and 3 layers. Plane 1 is only compatible
 * with layer 2 and plane 2 is only compatible with layer 3. Our algorithm will
 * discover the solution by building the mapping one plane at a time. It first
 * starts with plane 1: an atomic commit assigning layer 1 to plane 1 is
 * submitted. It fails, because this isn't supported by the driver. Then layer
 * 2 is assigned to plane 1 and the atomic test succeeds. We can go on and
 * repeat the operation with plane 2. After exploring the whole tree, we end up
 * with a valid allocation.
 *
 *
 *                    layer 1                 layer 1
 *                  +---------> failure     +---------> failure
 *                  |                       |
 *                  |                       |
 *                  |                       |
 *     +---------+  |          +---------+  |
 *     |         |  | layer 2  |         |  | layer 3   final allocation:
 *     | plane 1 +------------>+ plane 2 +--+---------> plane 1 → layer 2
 *     |         |  |          |         |              plane 2 → layer 3
 *     +---------+  |          +---------+
 *                  |
 *                  |
 *                  | layer 3
 *                  +---------> failure
 *
 *
 * Note how layer 2 isn't considered for plane 2: it's already mapped to plane
 * 1. Also note that branches are pruned as soon as an atomic test fails.
 *
 * In practice, the primary plane is treated separately. This is where layers
 * that can't be mapped to any plane (e.g. layer 1 in our example) will be
 * composited. The primary plane is the first that will be allocated, because
 * some drivers require it to be enabled in order to light up any other plane.
 * Then all other planes will be allocated, from the topmost one to the
 * bottommost one.
 *
 * The "zpos" property (which defines ordering between layers/planes) is handled
 * as a special case. If it's set on layers, it adds additional constraints on
 * their relative ordering. If two layers intersect, their relative zpos needs
 * to be preserved during plane allocation.
 *
 * Implementation-wise, the output_choose_layers function is called at each node
 * of the tree. It iterates over layers, check constraints, performs an atomic
 * test commit and calls itself recursively on the next plane.
 */

struct alloc_result
{
   drmModeAtomicReq *req;
   uint32_t flags;
   size_t planes_len;

   struct liftoff_rpi_layer **best;
   int best_score;

   bool has_comp_layer;
   size_t non_comp_layers_len;
};

struct alloc_step
{
   struct liftoff_rpi_list *plink;
   size_t pindex;

   struct liftoff_rpi_layer **alloc;
   int score, last_layer_zpos;
   int primary_layer_zpos, primary_plane_zpos;

   char log_prefix[64];

   bool composited;
};

static void
plane_step_init_next(struct alloc_step *step, struct alloc_step *prev, struct liftoff_rpi_layer *layer)
{
   struct liftoff_rpi_plane *plane;
   struct liftoff_rpi_property *zprop = NULL;
   size_t len;

   plane = liftoff_rpi_container_of(prev->plink, plane, link);
   step->plink = prev->plink->next;
   step->pindex = prev->pindex + 1;
   step->alloc = prev->alloc;
   step->alloc[prev->pindex] = layer;

   if (layer && layer == layer->output->comp_layer)
     {
        /* assert(!prev->composited); */
        step->composited = true;
     }
   else
     step->composited = prev->composited;

   if (layer && layer != layer->output->comp_layer)
     step->score = prev->score + 1;
   else
     step->score = prev->score;

   if (layer)
     zprop = layer_property_get(layer, LIFTOFF_RPI_PROP_ZPOS);

   if (zprop && plane->type != DRM_PLANE_TYPE_PRIMARY)
     step->last_layer_zpos = zprop->value;
   else
     step->last_layer_zpos = prev->last_layer_zpos;

   if (zprop && plane->type == DRM_PLANE_TYPE_PRIMARY)
     {
        step->primary_layer_zpos = zprop->value;
        step->primary_plane_zpos = plane->zpos;
     }
   else
     {
        step->primary_layer_zpos = prev->primary_layer_zpos;
        step->primary_plane_zpos = prev->primary_plane_zpos;
     }

   if (layer)
     {
        len = strlen(prev->log_prefix) + 2;
        if (len > sizeof(step->log_prefix) - 1)
          len = sizeof(step->log_prefix) - 1;
        memset(step->log_prefix, ' ', len);
        step->log_prefix[len] = '\0';
     }
   else
     memcpy(step->log_prefix, prev->log_prefix, sizeof(step->log_prefix));
}

static bool
layer_allocated_get(struct alloc_step *step, struct liftoff_rpi_layer *layer)
{
   size_t i = 0;

   for (; i < step->pindex; i++)
     {
        if (step->alloc[i] == layer)
          return true;
     }

   return false;
}

static bool
composited_layer_over_get(struct liftoff_rpi_output *output, struct alloc_step *step, struct liftoff_rpi_layer *layer)
{
   struct liftoff_rpi_layer *olayer;
   struct liftoff_rpi_property *zprop, *ozprop;

   zprop = layer_property_get(layer, LIFTOFF_RPI_PROP_ZPOS);
   if (!zprop) return false;

   liftoff_rpi_list_for_each(olayer, &output->layers, link)
     {
        if (layer_allocated_get(step, olayer))
          continue;

        ozprop = layer_property_get(olayer, LIFTOFF_RPI_PROP_ZPOS);
        if (!ozprop) continue;

        if (layer_intersects(layer, olayer) &&
            ozprop->value > zprop->value)
          return true;
     }

   return false;
}

static bool
allocated_layer_over_get(struct liftoff_rpi_output *output, struct alloc_step *step, struct liftoff_rpi_layer *layer)
{
   ssize_t i = -1;
   struct liftoff_rpi_plane *oplane;
   struct liftoff_rpi_layer *olayer;
   struct liftoff_rpi_property *zprop, *ozprop;

   zprop = layer_property_get(layer, LIFTOFF_RPI_PROP_ZPOS);
   if (!zprop) return false;

   liftoff_rpi_list_for_each(oplane, &output->dev->planes, link)
     {
        i++;
        if (i >= (ssize_t)step->pindex)
          break;

        if (oplane->type == DRM_PLANE_TYPE_PRIMARY)
          continue;

        olayer = step->alloc[i];
        if (!olayer) continue;

        ozprop = layer_property_get(olayer, LIFTOFF_RPI_PROP_ZPOS);
        if (!ozprop) continue;

        if (zprop->value > ozprop->value &&
            layer_intersects(layer, olayer))
          return true;
     }

   return false;
}

static bool
allocated_plane_under_get(struct liftoff_rpi_output *output, struct alloc_step *step, struct liftoff_rpi_layer *layer)
{
   struct liftoff_rpi_plane *plane, *oplane;
   ssize_t i = -1;

   plane = liftoff_rpi_container_of(step->plink, plane, link);

   liftoff_rpi_list_for_each(oplane, &output->dev->planes, link)
     {
        i++;
        if (i >= (ssize_t)step->pindex)
          break;

        if (oplane->type == DRM_PLANE_TYPE_PRIMARY)
          continue;

        if (!step->alloc[i]) continue;

        if (plane->zpos >= oplane->zpos &&
            layer_intersects(layer, step->alloc[i]))
          return true;
     }

   return false;
}

static bool
layer_plane_compatible_get(struct alloc_step *step, struct liftoff_rpi_layer *layer, struct liftoff_rpi_plane *plane)
{
   struct liftoff_rpi_output *output;
   struct liftoff_rpi_property *zprop;

   output = layer->output;

   if (layer_allocated_get(step, layer))
     return false;

   zprop = layer_property_get(layer, LIFTOFF_RPI_PROP_ZPOS);
   if (zprop != NULL)
     {
        if ((int)zprop->value > step->last_layer_zpos &&
            allocated_layer_over_get(output, step, layer))
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%s Layer %p -> plane %"PRIu32": "
                             "layer zpos invalid",
                             step->log_prefix, (void *)layer, plane->id);
             return false;
          }

        if ((int)zprop->value < step->last_layer_zpos &&
            allocated_plane_under_get(output, step, layer))
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%s Layer %p -> plane %"PRIu32": "
                             "plane zpos invalid",
                             step->log_prefix, (void *)layer, plane->id);
             return false;
          }

        if (plane->type != DRM_PLANE_TYPE_PRIMARY &&
            (int)zprop->value < step->primary_layer_zpos &&
            plane->zpos > step->primary_plane_zpos)
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%s Layer %p -> plane %"PRIu32": zpos %d "
                             "layer zpos %d under primary %d "
                             "Step primary layer zpos %d",
                             step->log_prefix, (void *)layer, plane->id,
                             plane->zpos,
                             (int)zprop->value, step->primary_plane_zpos,
                             step->primary_layer_zpos);
             return false;
          }
     }

   if (plane->type != DRM_PLANE_TYPE_PRIMARY &&
       composited_layer_over_get(output, step, layer))
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "%s Layer %p -> plane %"PRIu32": "
                        "has composited layer on top",
                        step->log_prefix, (void *)layer, plane->id);
        return false;
     }

   if (plane->type != DRM_PLANE_TYPE_PRIMARY &&
       layer == layer->output->comp_layer)
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "%s Layer %p -> plane %"PRIu32": "
                        "cannot put composition layer on "
                        "non-primary plane",
                        step->log_prefix, (void *)layer, plane->id);
        return false;
     }

   return true;
}

static bool
alloc_valid_get(struct liftoff_rpi_output *output, struct alloc_result *result, struct alloc_step *step)
{
   if (result->has_comp_layer && !step->composited &&
       step->score != (int)result->non_comp_layers_len)
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "%sCannot skip composition: some layers "
                        "are missing a plane", step->log_prefix);
        return false;
     }

   if (step->composited &&
       step->score == (int)result->non_comp_layers_len)
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "%sRefusing to use composition: all layers "
                        "have been put in a plane", step->log_prefix);
        return false;
     }

   /* TODO: check allocation isn't empty */

   return true;
}

static int
output_layers_choose(struct liftoff_rpi_output *output, struct alloc_result *result, struct alloc_step *step)
{
   struct liftoff_rpi_device *dev;
   struct liftoff_rpi_plane *plane;
   struct liftoff_rpi_layer *layer;
   int cur, ret;
   size_t rplanes;
   struct alloc_step nstep = {0};
   const char *type = NULL;

   dev = output->dev;

   if (step->plink == &dev->planes)
     {
        if (step->score > result->best_score &&
            alloc_valid_get(output, result, step))
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%sFound a better allocation with score=%d",
                             step->log_prefix, step->score);

             result->best_score = step->score;
             memcpy(result->best, step->alloc,
                    result->planes_len * sizeof(struct liftoff_rpi_layer *));
          }

        return 0;
     }

   plane = liftoff_rpi_container_of(step->plink, plane, link);

   rplanes = result->planes_len - step->pindex;
   if (result->best_score >= step->score + (int)rplanes)
     return 0;

   cur = drmModeAtomicGetCursor(result->req);

   if (plane->layer != NULL)
     goto skip;

   if ((plane->possible_crtcs & (1 << output->crtc_index)) == 0)
     goto skip;

   switch (plane->type)
     {
      case DRM_PLANE_TYPE_OVERLAY:
        type = "OVERLAY";
        break;
      case DRM_PLANE_TYPE_PRIMARY:
        type = "PRIMARY";
        break;
      case DRM_PLANE_TYPE_CURSOR:
        type = "CURSOR";
        break;
     }

   liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                   "%sPerforming allocation for plane %"PRIu32" %s (%zu/%zu)",
                   step->log_prefix, plane->id, type, step->pindex + 1,
                   result->planes_len);

   liftoff_rpi_list_for_each(layer, &output->layers, link)
     {
        if (layer->plane != NULL) continue;
        if (!layer_visible_get(layer))
          {
             /* liftoff_rpi_log(LIFTOFF_RPI_DEBUG, */
             /*                 "%s Layer %p Not Visible", */
             /*                 step->log_prefix, (void *)layer); */
             continue;
          }
        if (!layer_plane_compatible_get(step, layer, plane))
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%s Layer %p -> plane %"PRIu32": "
                             "Not Compatible",
                             step->log_prefix, (void *)layer, plane->id);
             continue;
          }

        ret = plane_apply(plane, layer, result->req);
        if (ret == -EINVAL)
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%s Layer %p -> plane %"PRIu32": "
                             "incompatible properties",
                             step->log_prefix, (void *)layer, plane->id);

             continue;
          }
        else if (ret != 0)
          return ret;

        layer_candidate_plane_add(layer, plane);

        if (layer->force_comp || !plane_check_layer_fb(plane, layer))
          {
             drmModeAtomicSetCursor(result->req, cur);
             continue;
          }

        ret = device_test_commit(dev, result->req, result->flags);
        if (ret == 0)
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%s Layer %p -> plane %"PRIu32" %s: success",
                             step->log_prefix, (void *)layer, plane->id, type);

             plane_step_init_next(&nstep, step, layer);
             ret = output_layers_choose(output, result, &nstep);
             if (ret != 0)
               return ret;
          }
        else if (ret != -EINVAL && ret != -ERANGE && ret != -ENOSPC)
          {
             /* liftoff_rpi_log(LIFTOFF_RPI_DEBUG, */
             /*                 "%s Layer %p -> plane %"PRIu32": " */
             /*                 "test-only commit failed (%s)", */
             /*                 step->log_prefix, (void *)layer, plane->id, */
             /*                 strerror(-ret)); */
             return ret;
          }
        else
          {
             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "%s Layer %p -> plane %"PRIu32": "
                             "test-only commit failed (%s)",
                             step->log_prefix, (void *)layer, plane->id,
                             strerror(-ret));
          }

        drmModeAtomicSetCursor(result->req, cur);
     }

skip:
   plane_step_init_next(&nstep, step, NULL);
   ret = output_layers_choose(output, result, &nstep);
   if (ret != 0) return ret;
   drmModeAtomicSetCursor(result->req, cur);
   return 0;
}

static int
apply_current(struct liftoff_rpi_output *output, drmModeAtomicReq *req)
{
   struct liftoff_rpi_plane *plane;
   int cur, ret;

   cur = drmModeAtomicGetCursor(req);

   liftoff_rpi_list_for_each(plane, &output->dev->planes, link)
     {
        ret = plane_apply(plane, plane->layer, req);
        if (ret != 0)
          {
             drmModeAtomicSetCursor(req, cur);
             return ret;
          }
     }

   return 0;
}

static bool
layer_fb_info_needs_realloc(const drmModeFB2 *a, const drmModeFB2 *b)
{
   if (a->width != b->width || a->height != b->height ||
       a->pixel_format != b->pixel_format || a->modifier != b->modifier) 
     return true;

   /* TODO: consider checking pitch and offset? */

   return false;
}

static void
layers_fb_info_update(struct liftoff_rpi_output *output)
{
   struct liftoff_rpi_layer *layer;

   liftoff_rpi_list_for_each(layer, &output->layers, link)
     {
        memset(&layer->fb_info, 0, sizeof(layer->fb_info));
        layer_cache_fb_info(layer);
     }
}

static bool
layer_realloc_get(struct liftoff_rpi_layer *layer)
{
   size_t i = 0;
   struct liftoff_rpi_property *prop;

   if (layer->changed) return true;

   for (; i < layer->props_len; i++)
     {
        prop = &layer->props[i];

        if (prop->index == LIFTOFF_RPI_PROP_FB_ID)
          {
             if (prop->value == 0 && prop->prev_value == 0)
               continue;

             if (prop->value == 0 || prop->prev_value == 0)
               return true;

             if (layer_fb_info_needs_realloc(&layer->fb_info, &layer->prev_fb_info))
               return true;

             continue;
          }

        if (prop->value == prop->prev_value) continue;

        if (prop->index == LIFTOFF_RPI_PROP_ALPHA)
          {
             if (prop->value == 0 || prop->prev_value == 0 ||
                 prop->value == 0xFFFF || prop->prev_value == 0xFFFF)
               return true;

             continue;
          }

        if (prop->index == LIFTOFF_RPI_PROP_IN_FENCE_FD ||
            prop->index == LIFTOFF_RPI_PROP_FB_DAMAGE_CLIPS)
          continue;

        /* TODO: if CRTC_{X,Y,W,H} changed but intersection with other
         * layers hasn't changed, don't realloc */
        return true;
     }

   return false;
}

static int
reuse_prev_alloc(struct liftoff_rpi_output *output, drmModeAtomicReq *req, uint32_t flags)
{
   struct liftoff_rpi_device *dev;
   struct liftoff_rpi_layer *layer;
   int cur, ret;

   dev = output->dev;
   if (output->layers_changed) return -EINVAL;

   liftoff_rpi_list_for_each(layer, &output->layers, link)
     {
        if (layer_realloc_get(layer))
          return -EINVAL;
     }

   cur = drmModeAtomicGetCursor(req);

   ret = apply_current(output, req);
   if (ret != 0) return ret;

   ret = device_test_commit(dev, req, flags);
   if (ret != 0) drmModeAtomicSetCursor(req, cur);

   return ret;
}

static void
layers_mark_clean(struct liftoff_rpi_output *output)
{
   struct liftoff_rpi_layer *layer;

   output->layers_changed = false;

   liftoff_rpi_list_for_each(layer, &output->layers, link)
     layer_clean(layer);
}

static void
layers_priority_update(struct liftoff_rpi_device *dev)
{
   struct liftoff_rpi_output *output;
   struct liftoff_rpi_layer *layer;
   bool elapsed;

   dev->page_flip_counter++;
   elapsed = (dev->page_flip_counter >= LIFTOFF_RPI_PRIORITY_PERIOD);
   if (elapsed) dev->page_flip_counter = 0;

   liftoff_rpi_list_for_each(output, &dev->outputs, link)
     {
        liftoff_rpi_list_for_each(layer, &output->layers, link)
          layer_priority_update(layer, elapsed);
     }
}

static void
log_reuse(struct liftoff_rpi_output *output)
{
   if (output->alloc_reused_counter == 0)
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "Reusing previous plane allocation on output %p",
                        (void *)output);
     }
   output->alloc_reused_counter++;
}

static void
log_no_reuse(struct liftoff_rpi_output *output)
{
   liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                   "Computing plane allocation on output %p",
                   (void *)output);

   if (output->alloc_reused_counter != 0)
     {
        liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                        "Stopped reusing previous plane allocation on "
                        "output %p (had reused it %d times)",
                        (void *)output, output->alloc_reused_counter);

        output->alloc_reused_counter = 0;
     }
}

static size_t
non_comp_layers_len(struct liftoff_rpi_output *output)
{
   struct liftoff_rpi_layer *layer;
   size_t n = 0;

   liftoff_rpi_list_for_each(layer, &output->layers, link)
     {
        if (layer_visible_get(layer) &&
            output->comp_layer != layer)
          n++;
     }

   return n;
}

int
liftoff_rpi_output_apply(struct liftoff_rpi_output *output, drmModeAtomicReq *req, uint32_t flags)
{
   struct liftoff_rpi_device *dev;
   struct liftoff_rpi_plane *plane;
   struct liftoff_rpi_layer *layer;
   struct alloc_result result = {0};
   struct alloc_step step = {0};
   size_t i = 0, cand = 0;
   const char *type = NULL;
   int ret;

   dev = output->dev;

   layers_priority_update(dev);
   layers_fb_info_update(output);

   ret = reuse_prev_alloc(output, req, flags);
   if (ret == 0)
     {
        log_reuse(output);
        return 0;
     }

   log_no_reuse(output);

   liftoff_rpi_list_for_each(layer, &output->layers, link)
     layer_candidate_planes_reset(layer);

   dev->test_commit_counter = 0;

   output_log_layers(output);

   liftoff_rpi_list_for_each(plane, &dev->planes, link)
     {
        if (plane->layer && plane->layer->output == output)
          {
             plane->layer->plane = NULL;
             plane->layer = NULL;
          }
     }

   liftoff_rpi_list_for_each(plane, &dev->planes, link)
     {
        if (plane->layer == NULL)
          {
             cand++;

             liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                             "Disabling plane %"PRIu32, plane->id);

             ret = plane_apply(plane, NULL, req);
             /* assert(ret != -EINVAL); */
             if (ret != 0) return ret;
          }
     }

   result.req = req;
   result.flags = flags;
   result.planes_len = liftoff_rpi_list_length(&dev->planes);

   step.alloc = malloc(result.planes_len * sizeof(*step.alloc));
   result.best = malloc(result.planes_len * sizeof(*result.best));
   if (step.alloc == NULL || result.best == NULL)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "malloc");
        return -ENOMEM;
     }

   result.best_score = -1;
   memset(result.best, 0, result.planes_len * sizeof(*result.best));
   result.has_comp_layer = (output->comp_layer != NULL);
   result.non_comp_layers_len = non_comp_layers_len(output);

   step.plink = dev->planes.next;
   step.pindex = 0;
   step.score = 0;
   step.last_layer_zpos = INT_MAX;
   step.primary_layer_zpos = INT_MIN;
   step.primary_plane_zpos = INT_MAX;
   step.composited = false;

   ret = output_layers_choose(output, &result, &step);
   if (ret != 0) return ret;

   liftoff_rpi_log(LIFTOFF_RPI_DEBUG,
                   "Found plane allocation for output %p (score: %d, candidate planes: %zu, tests: %d):",
                   (void *)output, result.best_score, cand,
                   dev->test_commit_counter);

   liftoff_rpi_list_for_each(plane, &dev->planes, link)
     {
        layer = result.best[i];
        i++;
        if (!layer) continue;

        switch (plane->type)
          {
           case DRM_PLANE_TYPE_OVERLAY:
             type = "OVERLAY";
             break;
           case DRM_PLANE_TYPE_PRIMARY:
             type = "PRIMARY";
             break;
           case DRM_PLANE_TYPE_CURSOR:
             type = "CURSOR";
             break;
          }

        liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "  Layer %p -> plane %"PRIu32" %s",
                        (void *)layer, plane->id, type);

        /* assert(plane->layer == NULL); */
        /* assert(layer->plane == NULL); */

        plane->layer = layer;
        layer->plane = plane;
     }

   if (i == 0)
     liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "No layer has a plane");

   ret = apply_current(output, req);
   if (ret != 0) return ret;

   free(step.alloc);
   free(result.best);

   layers_mark_clean(output);

   return 0;
}
