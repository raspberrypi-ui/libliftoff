#ifndef PRIVATE_H
# define PRIVATE_H

/* standard includes */
# include <errno.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <inttypes.h>
# include <limits.h>
# include <sys/types.h>

# include <libliftoff_rpi.h>
# include "log.h"
# include "list.h"

# define LIFTOFF_RPI_PRIORITY_PERIOD 60

struct liftoff_rpi_device
{
   int fd;

   struct liftoff_rpi_list planes;
   struct liftoff_rpi_list outputs;

   uint32_t *crtcs;
   size_t crtcs_len;

   size_t planes_cap;

   int test_commit_counter, page_flip_counter;
};

struct liftoff_rpi_output
{
   struct liftoff_rpi_device *dev;
   struct liftoff_rpi_list link;
   struct liftoff_rpi_list layers;
   struct liftoff_rpi_layer *comp_layer;

   uint32_t crtc_id;
   size_t crtc_index;

   int alloc_reused_counter;

   bool layers_changed;
};

struct liftoff_rpi_layer
{
   struct liftoff_rpi_output *output;
   struct liftoff_rpi_list link;
   struct liftoff_rpi_plane *plane;
   struct liftoff_rpi_property *props;

   uint32_t *candidate_planes;

   int current_priority, pending_priority;
   uint32_t props_len;

   bool force_comp, changed;

   drmModeFB2 fb_info, prev_fb_info;
};

struct liftoff_rpi_plane
{
   struct liftoff_rpi_list link;
   struct liftoff_rpi_layer *layer;
   struct liftoff_rpi_property *props;

   drmModePropertyBlobRes *in_formats_blob;
   size_t props_len;

   uint32_t id, type;
   uint32_t possible_crtcs;
   int zpos;
};

struct liftoff_rpi_property
{
   int index;
   uint32_t id;
   drmModePropertyRes *dprop;
   uint64_t value, prev_value;
};

struct liftoff_rpi_rect
{
   int x, y, w, h;
};

int device_test_commit(struct liftoff_rpi_device *dev, drmModeAtomicReq *req, uint32_t flags);

bool layer_visible_get(struct liftoff_rpi_layer *layer);
struct liftoff_rpi_property *layer_property_get(struct liftoff_rpi_layer *layer, int property);
bool layer_intersects(struct liftoff_rpi_layer *a, struct liftoff_rpi_layer *b);
void layer_clean(struct liftoff_rpi_layer *layer);
void layer_priority_update(struct liftoff_rpi_layer *layer, bool current);
bool layer_fb_get(struct liftoff_rpi_layer *layer);
void layer_candidate_plane_add(struct liftoff_rpi_layer *layer, struct liftoff_rpi_plane *plane);
void layer_candidate_planes_reset(struct liftoff_rpi_layer *layer);
int layer_cache_fb_info(struct liftoff_rpi_layer *layer);

int plane_apply(struct liftoff_rpi_plane *plane, struct liftoff_rpi_layer *layer, drmModeAtomicReq *req);
bool plane_check_layer_fb(struct liftoff_rpi_plane *plane, struct liftoff_rpi_layer *layer);
void output_log_layers(struct liftoff_rpi_output *output);

#endif
