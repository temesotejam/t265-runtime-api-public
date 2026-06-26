#include "../include/t265_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int t265_is_fisheye_sample_type(t265_queue_sample_type type)
{
    return type == T265_QUEUE_SAMPLE_FISHEYE0 || type == T265_QUEUE_SAMPLE_FISHEYE1;
}

static int t265_image_view_is_valid(const t265_image_view *view)
{
    if (!view || !view->data) {
        return 0;
    }
    if (view->width == 0 || view->height == 0 || view->frame_length == 0) {
        return 0;
    }
    return 1;
}

int t265_image_view_from_queue_sample(
    const t265_queue_sample *sample,
    t265_image_view *out_view
)
{
    if (!sample || !out_view) {
        return T265_ERR_INVALID_STATE;
    }

    memset(out_view, 0, sizeof(*out_view));

    if (!t265_is_fisheye_sample_type(sample->type)) {
        return T265_ERR_NOT_FOUND;
    }
    if (!sample->data.fisheye.frame_data) {
        return T265_ERR_NOT_FOUND;
    }
    if (sample->data.fisheye.width == 0 ||
        sample->data.fisheye.height == 0 ||
        sample->data.fisheye.frame_length == 0) {
        return T265_ERR_INVALID_STATE;
    }

    out_view->sensor_id = sample->data.fisheye.sensor_id;
    out_view->frame_id = sample->data.fisheye.frame_id;
    out_view->timestamp_ns = sample->data.fisheye.timestamp_ns;
    out_view->width = sample->data.fisheye.width;
    out_view->height = sample->data.fisheye.height;
    out_view->frame_length = sample->data.fisheye.frame_length;
    out_view->data = sample->data.fisheye.frame_data;

    return T265_OK;
}

int t265_owned_image_copy_from_view(
    const t265_image_view *view,
    t265_owned_image *out_image
)
{
    uint8_t *copy;

    if (!out_image) {
        return T265_ERR_INVALID_STATE;
    }

    memset(out_image, 0, sizeof(*out_image));

    if (!t265_image_view_is_valid(view)) {
        return T265_ERR_INVALID_STATE;
    }

    copy = (uint8_t *)malloc(view->frame_length);
    if (!copy) {
        return T265_ERR_USB;
    }

    memcpy(copy, view->data, view->frame_length);

    out_image->sensor_id = view->sensor_id;
    out_image->frame_id = view->frame_id;
    out_image->timestamp_ns = view->timestamp_ns;
    out_image->width = view->width;
    out_image->height = view->height;
    out_image->frame_length = view->frame_length;
    out_image->data = copy;

    return T265_OK;
}

int t265_owned_image_copy_from_queue_sample(
    const t265_queue_sample *sample,
    t265_owned_image *out_image
)
{
    t265_image_view view;
    int rc;

    rc = t265_image_view_from_queue_sample(sample, &view);
    if (rc != T265_OK) {
        if (out_image) {
            memset(out_image, 0, sizeof(*out_image));
        }
        return rc;
    }

    return t265_owned_image_copy_from_view(&view, out_image);
}

void t265_owned_image_destroy(t265_owned_image *image)
{
    if (!image) {
        return;
    }
    free(image->data);
    memset(image, 0, sizeof(*image));
}

int t265_write_pgm_from_view(const char *path, const t265_image_view *view)
{
    FILE *fp;
    size_t written;

    if (!path || !t265_image_view_is_valid(view)) {
        return T265_ERR_INVALID_STATE;
    }
    if (view->frame_length != view->width * view->height) {
        return T265_ERR_INVALID_STATE;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        return T265_ERR_USB;
    }

    if (fprintf(fp, "P5\n%u %u\n255\n", view->width, view->height) < 0) {
        fclose(fp);
        return T265_ERR_USB;
    }

    written = fwrite(view->data, 1, view->frame_length, fp);
    if (fclose(fp) != 0) {
        return T265_ERR_USB;
    }
    if (written != view->frame_length) {
        return T265_ERR_USB;
    }

    return T265_OK;
}

int t265_write_pgm_from_owned_image(const char *path, const t265_owned_image *image)
{
    t265_image_view view;

    if (!image) {
        return T265_ERR_INVALID_STATE;
    }

    memset(&view, 0, sizeof(view));
    view.sensor_id = image->sensor_id;
    view.frame_id = image->frame_id;
    view.timestamp_ns = image->timestamp_ns;
    view.width = image->width;
    view.height = image->height;
    view.frame_length = image->frame_length;
    view.data = image->data;

    return t265_write_pgm_from_view(path, &view);
}
