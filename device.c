#include "private.h"

/* local functions */
int
device_test_commit(struct liftoff_rpi_device *dev, drmModeAtomicReq *req, uint32_t flags)
{
   int ret;

   dev->test_commit_counter++;

   flags &= ~(uint32_t)DRM_MODE_PAGE_FLIP_EVENT;
   do
     {
        ret = drmModeAtomicCommit(dev->fd, req,
                                  DRM_MODE_ATOMIC_TEST_ONLY | flags, NULL);
     } while (ret == -EINTR || ret == -EAGAIN);

   /* The kernel will return -EINVAL for invalid configuration, -ERANGE for
    * CRTC coords overflow, and -ENOSPC for invalid SRC coords. */
   if (ret != 0 && ret != -EINVAL && ret != -ERANGE && ret != -ENOSPC)
     {
        liftoff_rpi_log(LIFTOFF_RPI_ERROR, "drmModeAtomicCommit: %s",
                        strerror(-ret));
     }

   return ret;
}

/* API functions */
struct liftoff_rpi_device *
liftoff_rpi_device_create(int fd)
{
   struct liftoff_rpi_device *dev;
   drmModeRes *res;
   drmModePlaneRes *pres;

   dev = calloc(1, sizeof(*dev));
   if (!dev)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "calloc");
        return NULL;
     }

   liftoff_rpi_list_init(&dev->planes);
   liftoff_rpi_list_init(&dev->outputs);

   dev->fd = dup(fd);
   if (dev->fd < 0)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "dup");
        free(dev);
        return NULL;
     }

   res = drmModeGetResources(fd);
   if (!res)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "drmModeGetResources");
        liftoff_rpi_device_destroy(dev);
        return NULL;
     }

   dev->crtcs_len = (size_t)res->count_crtcs;
   dev->crtcs = malloc(dev->crtcs_len * sizeof(dev->crtcs[0]));
   if (!dev->crtcs)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "malloc");
        liftoff_rpi_device_destroy(dev);
        drmModeFreeResources(res);
        return NULL;
     }

   memcpy(dev->crtcs, res->crtcs, dev->crtcs_len * sizeof(dev->crtcs[0]));

   drmModeFreeResources(res);

   pres = drmModeGetPlaneResources(dev->fd);
   if (!pres)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "drmModeGetPlaneResouces");
        liftoff_rpi_device_destroy(dev);
        return NULL;
     }

   dev->planes_cap = pres->count_planes;
   drmModeFreePlaneResources(pres);

   return dev;
}

void
liftoff_rpi_device_destroy(struct liftoff_rpi_device *dev)
{
   struct liftoff_rpi_plane *plane, *tmp;

   if (!dev) return;

   close(dev->fd);

   liftoff_rpi_list_for_each_safe(plane, tmp, &dev->planes, link)
     liftoff_rpi_plane_destroy(plane);

   free(dev->crtcs);
   free(dev);
}

int
liftoff_rpi_device_register_planes(struct liftoff_rpi_device *dev)
{
   drmModePlaneRes *res;
   uint32_t i = 0;

   res = drmModeGetPlaneResources(dev->fd);
   if (!res)
     {
        liftoff_rpi_log_errno(LIFTOFF_RPI_ERROR, "drmModeGetPlaneResouces");
        return -errno;
     }

   for (; i < res->count_planes; i++)
     {
        if (!liftoff_rpi_plane_create(dev, res->planes[i]))
          {
             liftoff_rpi_log(LIFTOFF_RPI_ERROR, "Failed to create plane");
             drmModeFreePlaneResources(res);
             return -errno;
          }
     }

   drmModeFreePlaneResources(res);
   return 0;
}
