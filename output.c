#include "private.h"

static double
fp16_to_double(uint64_t val)
{
   return (double)(val >> 16) + (double)(val & 0xFFFF) / 0xFFFF;
}

void
output_log_layers(struct liftoff_rpi_output *output)
{
   struct liftoff_rpi_layer *layer;
   size_t i;
   bool is_composition_layer;

   if (!liftoff_rpi_log_has(LIFTOFF_RPI_DEBUG)) {
      return;
   }

   liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "Layers on CRTC %"PRIu32" (%zu total):",
               output->crtc_id, liftoff_rpi_list_length(&output->layers));

   liftoff_rpi_list_for_each(layer, &output->layers, link) {
      if (layer->force_comp) {
         liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "  Layer %p "
                     "(forced composition):", (void *)layer);
      } else {
         /* if (!layer_fb_get(layer)) { */
         /*    continue; */
         /* } */
         is_composition_layer = output->comp_layer == layer;
         liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "  Layer %p%s:",
                     (void *)layer, is_composition_layer ?
                     " (composition layer)" : "");
      }

      for (i = 0; i < layer->props_len; i++) {
         char *name = NULL;
         uint64_t value = layer->props[i].value;

         switch (layer->props[i].index)
           {
            case LIFTOFF_RPI_PROP_TYPE:
              name = "type";
              break;
            case LIFTOFF_RPI_PROP_CRTC_X:
              name = "CRTC_X";
              break;
            case LIFTOFF_RPI_PROP_CRTC_Y:
              name = "CRTC_Y";
              break;
            case LIFTOFF_RPI_PROP_SRC_X:
              name = "SRC_X";
              break;
            case LIFTOFF_RPI_PROP_SRC_Y:
              name = "SRC_Y";
              break;
            case LIFTOFF_RPI_PROP_SRC_W:
              name = "SRC_W";
              break;
            case LIFTOFF_RPI_PROP_SRC_H:
              name = "SRC_H";
              break;
            case LIFTOFF_RPI_PROP_FB_ID:
              name = "FB_ID";
              break;
            default:
              break;
           }

         if (!name) continue;

         if (strcmp(name, "CRTC_X") == 0 ||
             strcmp(name, "CRTC_Y") == 0) {
            liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "    %s = %+"PRIi32,
                        name, (int32_t)value);
         } else if (strcmp(name, "SRC_X") == 0 ||
                    strcmp(name, "SRC_Y") == 0 ||
                    strcmp(name, "SRC_W") == 0 ||
                    strcmp(name, "SRC_H") == 0) {
            liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "    %s = %f",
                        name, fp16_to_double(value));
         } else if (strcmp(name, "FB_ID") == 0 ||
                    strcmp(name, "type") == 0) {
            liftoff_rpi_log(LIFTOFF_RPI_DEBUG, "    %s = %"PRIu64,
                        name, value);
         }
      }
   }
}

struct liftoff_rpi_output *
liftoff_rpi_output_create(struct liftoff_rpi_device *dev, uint32_t crtc_id)
{
   struct liftoff_rpi_output *output;
   ssize_t crtc_index = -1;
   size_t i = 0;

   for (; i < dev->crtcs_len; i++)
     {
        if (dev->crtcs[i] == crtc_id)
          {
             crtc_index = (ssize_t)i;
             break;
          }
     }

   if (crtc_index < 0) return NULL;

   output = calloc(1, sizeof(*output));
   if (!output)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "calloc");
        return NULL;
     }

   output->dev = dev;
   output->crtc_id = crtc_id;
   output->crtc_index = (size_t)crtc_index;

   liftoff_rpi_list_init(&output->layers);
   liftoff_rpi_list_insert(&dev->outputs, &output->link);

   return output;
}

void
liftoff_rpi_output_destroy(struct liftoff_rpi_output *output)
{
   if (!output) return;
   liftoff_rpi_list_remove(&output->link);
   free(output);
}

void
liftoff_rpi_output_composition_layer_set(struct liftoff_rpi_output *output, struct liftoff_rpi_layer *layer)
{
   if (layer->output != output) return;

   if (layer != output->comp_layer) output->layers_changed = true;
   output->comp_layer = layer;
}

bool
liftoff_rpi_output_needs_composition(struct liftoff_rpi_output *output)
{
   struct liftoff_rpi_layer *layer;

   liftoff_rpi_list_for_each(layer, &output->layers, link)
     {
        if (liftoff_rpi_layer_needs_composition(layer))
          return true;
     }

   return false;
}
