#ifndef LIFTOFF_RPI_H
# define LIFTOFF_RPI_H

# include <stdarg.h>
# include <stdbool.h>
# include <stdint.h>
# include <stddef.h>
# include <xf86drm.h>
# include <xf86drmMode.h>

enum liftoff_rpi_log_priority
{
   LIFTOFF_RPI_SILENT,
   LIFTOFF_RPI_ERROR,
   LIFTOFF_RPI_DEBUG,
};

/* NB: If additional properties get added here, be sure to update the 
 * liftoff_rpi_plane_create function in plane.c */
enum liftoff_rpi_property_name
{
   LIFTOFF_RPI_PROP_TYPE = 1,
   LIFTOFF_RPI_PROP_FB_ID = 2,
   LIFTOFF_RPI_PROP_CRTC_ID = 3,
   LIFTOFF_RPI_PROP_CRTC_X = 4,
   LIFTOFF_RPI_PROP_CRTC_Y = 5,
   LIFTOFF_RPI_PROP_CRTC_W = 6,
   LIFTOFF_RPI_PROP_CRTC_H = 7,
   LIFTOFF_RPI_PROP_SRC_X = 8,
   LIFTOFF_RPI_PROP_SRC_Y = 9,
   LIFTOFF_RPI_PROP_SRC_W = 10,
   LIFTOFF_RPI_PROP_SRC_H = 11,
   LIFTOFF_RPI_PROP_ZPOS = 12,
   LIFTOFF_RPI_PROP_ALPHA = 13,
   LIFTOFF_RPI_PROP_ROTATION = 14,
   LIFTOFF_RPI_PROP_SCALING_FILTER = 15,
   LIFTOFF_RPI_PROP_PIXEL_BLEND_MODE = 16,
   LIFTOFF_RPI_PROP_FB_DAMAGE_CLIPS = 17,
   LIFTOFF_RPI_PROP_IN_FENCE_FD = 18,
   LIFTOFF_RPI_PROP_IN_FORMATS = 19,
};

struct liftoff_rpi_device;
struct liftoff_rpi_output;
struct liftoff_rpi_layer;
struct liftoff_rpi_plane;
struct liftoff_rpi_property;

/* API log functions */
typedef void (*liftoff_rpi_log_handler)(enum liftoff_rpi_log_priority priority,
                                        const char *fmt, va_list args);

void liftoff_rpi_log_priority_set(enum liftoff_rpi_log_priority priority);
void liftoff_rpi_log_handler_set(liftoff_rpi_log_handler handler);

/* API device functions */
struct liftoff_rpi_device *liftoff_rpi_device_create(int fd);
void liftoff_rpi_device_destroy(struct liftoff_rpi_device *dev);
int liftoff_rpi_device_register_planes(struct liftoff_rpi_device *dev);

/* API output functions */
struct liftoff_rpi_output *liftoff_rpi_output_create(struct liftoff_rpi_device *dev, uint32_t crtc_id);
void liftoff_rpi_output_destroy(struct liftoff_rpi_output *output);
void liftoff_rpi_output_composition_layer_set(struct liftoff_rpi_output *output, struct liftoff_rpi_layer *layer);
bool liftoff_rpi_output_needs_composition(struct liftoff_rpi_output *output);
int liftoff_rpi_output_apply(struct liftoff_rpi_output *output, drmModeAtomicReq *req, uint32_t flags);

/* API layer functions */
struct liftoff_rpi_layer *liftoff_rpi_layer_create(struct liftoff_rpi_output *output);
void liftoff_rpi_layer_destroy(struct liftoff_rpi_layer *layer);
bool liftoff_rpi_layer_needs_composition(struct liftoff_rpi_layer *layer);
int liftoff_rpi_layer_property_set(struct liftoff_rpi_layer *layer, int property, uint64_t value);
void liftoff_rpi_layer_property_unset(struct liftoff_rpi_layer *layer, int property);
void liftoff_rpi_layer_fb_composited_set(struct liftoff_rpi_layer *layer);
struct liftoff_rpi_plane *liftoff_rpi_layer_plane_get(struct liftoff_rpi_layer *layer);
bool liftoff_rpi_layer_visible_get(struct liftoff_rpi_layer *layer);

/* API plane functions */
struct liftoff_rpi_plane *liftoff_rpi_plane_create(struct liftoff_rpi_device *dev, uint32_t id);
void liftoff_rpi_plane_destroy(struct liftoff_rpi_plane *plane);
uint32_t liftoff_rpi_plane_id_get(struct liftoff_rpi_plane *plane);
uint32_t liftoff_rpi_plane_type_get(struct liftoff_rpi_plane *plane);

bool liftoff_rpi_layer_candidate_plane_get(struct liftoff_rpi_layer *layer, struct liftoff_rpi_plane *plane);

#endif
