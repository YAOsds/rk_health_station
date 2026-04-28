#include <gst/app/gstappsink.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include <iostream>
#include <sys/stat.h>
#include <string>

namespace {

std::string boolText(bool value) {
    return value ? "true" : "false";
}

int parsePositiveInt(const char *text, int fallback) {
    if (!text || !*text) {
        return fallback;
    }
    int value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return fallback;
        }
        value = value * 10 + (*p - '0');
        if (value > 1000) {
            return fallback;
        }
    }
    return value > 0 ? value : fallback;
}

GstPadProbeReturn handleAllocationQuery(GstPad *, GstPadProbeInfo *info, gpointer) {
    if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM)) {
        return GST_PAD_PROBE_OK;
    }
    GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);
    if (!query || GST_QUERY_TYPE(query) != GST_QUERY_ALLOCATION) {
        return GST_PAD_PROBE_OK;
    }

    GstCaps *caps = nullptr;
    gboolean needPool = FALSE;
    gst_query_parse_allocation(query, &caps, &needPool);
    if (!caps) {
        return GST_PAD_PROBE_OK;
    }

    GstVideoInfo videoInfo;
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        return GST_PAD_PROBE_OK;
    }

    GstBufferPool *pool = gst_buffer_pool_new();
    GstStructure *config = gst_buffer_pool_get_config(pool);
    const guint size = static_cast<guint>(GST_VIDEO_INFO_SIZE(&videoInfo));
    gst_buffer_pool_config_set_params(config, caps, size, 2, 8);
    gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!gst_buffer_pool_set_config(pool, config)) {
        gst_object_unref(pool);
        return GST_PAD_PROBE_OK;
    }

    gst_query_add_allocation_pool(query, pool, size, 2, 8);
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, nullptr);
    gst_object_unref(pool);
    std::cout << "allocation_query_answered size=" << size << " need_pool=" << boolText(needPool) << '\n';
    return GST_PAD_PROBE_HANDLED;
}

void printMemoryInfo(GstBuffer *buffer) {
    const guint memoryCount = gst_buffer_n_memory(buffer);
    std::cout << "memory_count=" << memoryCount << '\n';
    for (guint i = 0; i < memoryCount; ++i) {
        GstMemory *memory = gst_buffer_peek_memory(buffer, i);
        if (!memory) {
            std::cout << "memory[" << i << "] missing\n";
            continue;
        }
        gsize offset = 0;
        gsize maxSize = 0;
        const gsize size = gst_memory_get_sizes(memory, &offset, &maxSize);
        const char *allocatorType = (memory->allocator && memory->allocator->mem_type)
            ? memory->allocator->mem_type : "none";
        std::cout << "memory[" << i << "] dmabuf=" << boolText(gst_is_dmabuf_memory(memory))
                  << " allocator=" << allocatorType
                  << " is_system=" << boolText(gst_memory_is_type(memory, GST_ALLOCATOR_SYSMEM))
                  << " size=" << size
                  << " offset=" << offset
                  << " max_size=" << maxSize;
        if (gst_is_dmabuf_memory(memory)) {
            const int fd = gst_dmabuf_memory_get_fd(memory);
            std::cout << " fd=" << fd;
            struct stat st {};
            if (fd >= 0 && fstat(fd, &st) == 0) {
                std::cout << " dev=" << static_cast<unsigned long long>(st.st_dev)
                          << " ino=" << static_cast<unsigned long long>(st.st_ino)
                          << " mode=" << static_cast<unsigned int>(st.st_mode);
            }
        }
        std::cout << '\n';
    }

    if (GstVideoMeta *meta = gst_buffer_get_video_meta(buffer)) {
        std::cout << "video_meta planes=" << static_cast<int>(meta->n_planes);
        for (guint i = 0; i < meta->n_planes; ++i) {
            std::cout << " plane" << i << "_offset=" << meta->offset[i]
                      << " plane" << i << "_stride=" << meta->stride[i];
        }
        std::cout << '\n';
    } else {
        std::cout << "video_meta none\n";
    }
}

} // namespace

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    if (argc < 2) {
        std::cerr << "usage: gst_dmabuf_appsink_probe '<pipeline with appsink name=sink>' [samples]\n";
        return 2;
    }
    const int samples = argc >= 3 ? parsePositiveInt(argv[2], 5) : 5;
    const bool answerAllocation = argc >= 4 && std::string(argv[3]) == "answer-allocation";

    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(argv[1], &error);
    if (!pipeline) {
        std::cerr << "parse_failed: " << (error ? error->message : "unknown") << '\n';
        if (error) {
            g_error_free(error);
        }
        return 3;
    }
    if (error) {
        std::cerr << "parse_warning: " << error->message << '\n';
        g_error_free(error);
    }

    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sink || !GST_IS_APP_SINK(sink)) {
        std::cerr << "appsink name=sink missing\n";
        if (sink) {
            gst_object_unref(sink);
        }
        gst_object_unref(pipeline);
        return 4;
    }

    gst_app_sink_set_emit_signals(GST_APP_SINK(sink), false);
    gst_app_sink_set_drop(GST_APP_SINK(sink), true);
    gst_app_sink_set_max_buffers(GST_APP_SINK(sink), 1);
    if (answerAllocation) {
        GstPad *sinkPad = gst_element_get_static_pad(sink, "sink");
        if (sinkPad) {
            gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
                &handleAllocationQuery, nullptr, nullptr);
            gst_object_unref(sinkPad);
        }
    }

    const GstStateChangeReturn setState = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (setState == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "set_playing_failed\n";
        gst_object_unref(sink);
        gst_object_unref(pipeline);
        return 5;
    }

    bool sawDmaBuf = false;
    for (int i = 0; i < samples; ++i) {
        GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
        if (!sample) {
            std::cerr << "sample_timeout index=" << i << '\n';
            break;
        }
        GstCaps *caps = gst_sample_get_caps(sample);
        gchar *capsText = caps ? gst_caps_to_string(caps) : nullptr;
        std::cout << "sample=" << i << " caps=" << (capsText ? capsText : "none") << '\n';
        if (capsText) {
            g_free(capsText);
        }
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        if (buffer) {
            printMemoryInfo(buffer);
            for (guint m = 0; m < gst_buffer_n_memory(buffer); ++m) {
                GstMemory *memory = gst_buffer_peek_memory(buffer, m);
                sawDmaBuf = sawDmaBuf || (memory && gst_is_dmabuf_memory(memory));
            }
        }
        gst_sample_unref(sample);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(sink);
    gst_object_unref(pipeline);

    std::cout << "summary saw_dmabuf=" << boolText(sawDmaBuf) << '\n';
    return sawDmaBuf ? 0 : 10;
}
