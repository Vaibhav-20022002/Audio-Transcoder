#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avstring.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavutil/error.h>

#define OUTPUT_BIT_RATE 96000
#define OUTPUT_CHANNELS 2

static char *get_error_text(const int error)
{
    static char error_buffer[255];
    av_strerror(error, error_buffer, sizeof(error_buffer));
    return error_buffer;
}

static int open_input_file(const char *filename,
                           AVFormatContext **input_format_context,
                           AVCodecContext **input_codec_context)
{
    AVCodecContext *avctx = NULL;
    const AVCodec *input_codec = NULL;
    int error;

    *input_format_context = avformat_alloc_context();
    if (!*input_format_context) {
        fprintf(stderr, "Could not allocate input format context\n");
        return AVERROR(ENOMEM);
    }

    if ((error = avformat_open_input(input_format_context, filename, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s' (error '%s')\n", filename, av_err2str(error));
        avformat_free_context(*input_format_context);
        *input_format_context = NULL;
        return error;
    }

    if ((error = avformat_find_stream_info(*input_format_context, NULL)) < 0) {
        fprintf(stderr, "Could not find stream info (error '%s')\n", get_error_text(error));
        avformat_close_input(input_format_context);
        *input_format_context = NULL;
        return error;
    }

    if ((*input_format_context)->nb_streams != 1) {
        fprintf(stderr, "Expected one audio input stream, but found %d\n", (*input_format_context)->nb_streams);
        avformat_close_input(input_format_context);
        *input_format_context = NULL;
        return AVERROR_EXIT;
    }

    if (!(input_codec = avcodec_find_decoder((*input_format_context)->streams[0]->codecpar->codec_id))) {
        fprintf(stderr, "Could not find input codec\n");
        avformat_close_input(input_format_context);
        *input_format_context = NULL;
        return AVERROR_EXIT;
    }

    if ((*input_format_context)->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        fprintf(stderr, "The input file does not contain an audio stream.\n");
        avformat_close_input(input_format_context);
        *input_format_context = NULL;
        return AVERROR_INVALIDDATA;
    }

    printf("Debug: Input audio format - Sample rate: %d, Channels: %d, Sample format: %d\n",
            (*input_format_context)->streams[0]->codecpar->sample_rate,
            (*input_format_context)->streams[0]->codecpar->ch_layout.nb_channels,
            (*input_format_context)->streams[0]->codecpar->format);

    avctx = avcodec_alloc_context3(input_codec);
    if (!avctx) {
        fprintf(stderr, "Could not allocate a decoding context\n");
        avformat_close_input(input_format_context);
        *input_format_context = NULL;
        return AVERROR(ENOMEM);
    }

    error = avcodec_parameters_to_context(avctx, (*input_format_context)->streams[0]->codecpar);
    if (error < 0) {
        avformat_close_input(input_format_context);
        avcodec_free_context(&avctx);
        *input_format_context = NULL;
        return error;
    }

    if ((error = avcodec_open2(avctx, input_codec, NULL)) < 0) {
        fprintf(stderr, "Could not open input codec (error '%s')\n", get_error_text(error));
        avcodec_free_context(&avctx);
        avformat_close_input(input_format_context);
        *input_format_context = NULL;
        return error;
    }

    *input_codec_context = avctx;

    return 0;
}

static int init_input_frame(AVFrame **frame)
{
    *frame = av_frame_alloc();
    if (!*frame) {
        fprintf(stderr, "Could not allocate input frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

static int open_output_file(const char *filename,
                            AVCodecContext *input_codec_context,
                            AVFormatContext **output_format_context,
                            AVCodecContext **output_codec_context)
{
    AVCodecContext *avctx = NULL;
    AVIOContext *output_io_context = NULL;
    AVStream *stream = NULL;
    const AVCodec *output_codec = NULL;
    int error;

    if ((error = avio_open(&output_io_context, filename, AVIO_FLAG_WRITE)) < 0) {
        fprintf(stderr, "Could not open output file '%s' (error '%s')\n", filename, get_error_text(error));
        return error;
    }

    *output_format_context = avformat_alloc_context();
    if (!*output_format_context) {
        fprintf(stderr, "Could not allocate output format context\n");
        avio_closep(&output_io_context);
        return AVERROR(ENOMEM);
    }

    (*output_format_context)->pb = output_io_context;

    const AVOutputFormat *output_format = av_guess_format(NULL, filename, NULL);
    printf("after av_guess_format\n");
    if (!output_format) {
        fprintf(stderr, "Could not find output file format\n");
        avio_closep(&output_io_context);
        avformat_free_context(*output_format_context);
        *output_format_context = NULL;
        printf("exiting the progrma from av_guess_format\n");
        return AVERROR_EXIT;
    }

    // Set the output format
    (*output_format_context)->oformat = output_format;

    // av_strlcpy((*output_format_context)->url, filename, sizeof((*output_format_context)->url));
    // Ensure the URL field is properly handled or skip if not necessary
    // Check if the URL field is valid or if the filename length fits
    // if (filename && av_strlcpy((*output_format_context)->url, filename, sizeof((*output_format_context)->url)) >= sizeof((*output_format_context)->url)) {
    //     printf("inside if\n");
    //     fprintf(stderr, "Filename is too long to fit in the URL buffer\n");
    //     avio_closep(&output_io_context);
    //     avformat_free_context(*output_format_context);
    //     *output_format_context = NULL;
    //     printf("closing strlcpy\n");
    //     return AVERROR_EXIT;
    // }
    // printf("#3\n");

    output_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!output_codec) {
        fprintf(stderr, "Could not find an AAC encoder.\n");
        avio_closep(&output_io_context);
        avformat_free_context(*output_format_context);
        *output_format_context = NULL;
        return AVERROR_EXIT;
    }

    stream = avformat_new_stream(*output_format_context, NULL);
    if (!stream) {
        fprintf(stderr, "Could not create new stream\n");
        avio_closep(&output_io_context);
        avformat_free_context(*output_format_context);
        *output_format_context = NULL;
        return AVERROR(ENOMEM);
    }

    avctx = avcodec_alloc_context3(output_codec);
    if (!avctx) {
        fprintf(stderr, "Could not allocate an encoding context\n");
        avio_closep(&output_io_context);
        avformat_free_context(*output_format_context);
        *output_format_context = NULL;
        return AVERROR(ENOMEM);
    }

    avctx->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    avctx->sample_rate = input_codec_context->sample_rate;
    avctx->sample_fmt = output_codec->sample_fmts[0];
    avctx->bit_rate = OUTPUT_BIT_RATE;
    avctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    stream->time_base.den = input_codec_context->sample_rate;
    stream->time_base.num = 1;

    if ((*output_format_context)->oformat->flags & AVFMT_GLOBALHEADER)
        avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    printf("GOING INSIDE : (avcodec_open2)[193]\n");
    if ((error = avcodec_open2(avctx, output_codec, NULL)) < 0) {
        fprintf(stderr, "Could not open output codec (error '%s')\n", get_error_text(error));
        avcodec_free_context(&avctx);
        avio_closep(&output_io_context);
        avformat_free_context(*output_format_context);
        *output_format_context = NULL;
        return error;
    }

    printf("GOING INSIDE : (avcodec_parameters_from_context)[203]\n");
    error = avcodec_parameters_from_context(stream->codecpar, avctx);
    if (error < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        avcodec_free_context(&avctx);
        avio_closep(&output_io_context);
        avformat_free_context(*output_format_context);
        *output_format_context = NULL;
        return error;
    }

    *output_codec_context = avctx;

    printf("EASILY RETURNED\n");
    return 0;
}

static int init_resampler(AVCodecContext *input_codec_context,
                          AVCodecContext *output_codec_context,
                          SwrContext **resample_context)
{
    int error;

    *resample_context = swr_alloc();
    if (!*resample_context) {
        fprintf(stderr, "Could not allocate resample context\n");
        return AVERROR(ENOMEM);
    }

    av_opt_set_chlayout(*resample_context, "in_chlayout", &input_codec_context->ch_layout, 0);
    av_opt_set_chlayout(*resample_context, "out_chlayout", &output_codec_context->ch_layout, 0);
    av_opt_set_int(*resample_context, "in_sample_rate", input_codec_context->sample_rate, 0);
    av_opt_set_int(*resample_context, "out_sample_rate", output_codec_context->sample_rate, 0);
    av_opt_set_sample_fmt(*resample_context, "in_sample_fmt", input_codec_context->sample_fmt, 0);
    av_opt_set_sample_fmt(*resample_context, "out_sample_fmt", output_codec_context->sample_fmt, 0);

    if ((error = swr_init(*resample_context)) < 0) {
        fprintf(stderr, "Could not open resample context\n");
        swr_free(resample_context);
        *resample_context = NULL;
        return error;
    }
    return 0;
}

static int init_fifo(AVAudioFifo **fifo, AVCodecContext *output_codec_context)
{
    if (!(*fifo = av_audio_fifo_alloc(output_codec_context->sample_fmt,
                                      output_codec_context->ch_layout.nb_channels, 1))) {
        fprintf(stderr, "Could not allocate FIFO\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

static int write_output_file_header(AVFormatContext *output_format_context)
{
    int error;
    if ((error = avformat_write_header(output_format_context, NULL)) < 0) {
        fprintf(stderr, "Could not write output file header (error '%s')\n", get_error_text(error));
        return error;
    }
    return 0;
}

static int decode_audio_frame(AVFrame *frame,
                              AVFormatContext *input_format_context,
                              AVCodecContext *input_codec_context,
                              int *data_present, int *finished)
{
    static AVPacket input_packet;
    int error;

    if ((error = av_read_frame(input_format_context, &input_packet)) < 0) {
        if (error == AVERROR_EOF) {
            *finished = 1;
        } else {
            fprintf(stderr, "Could not read frame (error '%s')\n", get_error_text(error));
            return error;
        }
    }

    if ((error = avcodec_send_packet(input_codec_context, &input_packet)) < 0) {
        fprintf(stderr, "Could not send packet for decoding (error '%s')\n", get_error_text(error));
        av_packet_unref(&input_packet);
        return error;
    }

    error = avcodec_receive_frame(input_codec_context, frame);
    if (error == AVERROR(EAGAIN)) {
        error = 0;
    } else if (error == AVERROR_EOF) {
        *finished = 1;
        error = 0;
    } else if (error < 0) {
        fprintf(stderr, "Could not decode frame (error '%s')\n", get_error_text(error));
        av_packet_unref(&input_packet);
        return error;
    } else {
        *data_present = 1;
    }

    av_packet_unref(&input_packet);
    return 0;
}

static int init_converted_samples(uint8_t ***converted_input_samples,
                                  AVCodecContext *output_codec_context,
                                  int frame_size)
{
    int error;
    if (!(*converted_input_samples = (uint8_t **)calloc(output_codec_context->ch_layout.nb_channels, sizeof(**converted_input_samples)))) {
        fprintf(stderr, "Could not allocate converted input sample pointers\n");
        return AVERROR(ENOMEM);
    }

    if ((error = av_samples_alloc(*converted_input_samples, NULL,
                                  output_codec_context->ch_layout.nb_channels,
                                  frame_size, output_codec_context->sample_fmt, 0)) < 0) {
        fprintf(stderr, "Could not allocate converted input samples (error '%s')\n", get_error_text(error));
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        *converted_input_samples = NULL;
        return error;
    }
    return 0;
}

static int convert_samples(const uint8_t **input_data,
                           uint8_t **converted_data, const int frame_size,
                           SwrContext *resample_context)
{
    int error;

    if ((error = swr_convert(resample_context,
                             converted_data, frame_size,
                             input_data, frame_size)) < 0) {
        fprintf(stderr, "Could not convert input samples (error '%s')\n", get_error_text(error));
        return error;
    }
    return 0;
}

static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size)
{
    int error;

    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
        fprintf(stderr, "Could not reallocate FIFO\n");
        return error;
    }

    if (av_audio_fifo_write(fifo, (void **)converted_input_samples, frame_size) < frame_size) {
        fprintf(stderr, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

static int read_decode_convert_and_store(AVAudioFifo *fifo, AVFormatContext *input_format_context,
                                         AVCodecContext *input_codec_context, AVCodecContext *output_codec_context,
                                         SwrContext *resample_context, int *finished)
{
    AVFrame *input_frame = NULL;
    uint8_t **converted_input_samples = NULL;
    int data_present = 0;
    int ret = AVERROR_EXIT;

    if (init_input_frame(&input_frame))
        goto cleanup;

    if (decode_audio_frame(input_frame, input_format_context,
                           input_codec_context, &data_present, finished))
        goto cleanup;

    if (*finished && !data_present) {
        ret = 0;
        goto cleanup;
    }

    if (data_present) {
        if (init_converted_samples(&converted_input_samples, output_codec_context, input_frame->nb_samples))
            goto cleanup;

        if (convert_samples((const uint8_t **)input_frame->extended_data, converted_input_samples,
                            input_frame->nb_samples, resample_context))
            goto cleanup;

        if (add_samples_to_fifo(fifo, converted_input_samples, input_frame->nb_samples))
            goto cleanup;
        ret = 0;
    }

cleanup:
    if (converted_input_samples) {
        av_freep(&converted_input_samples[0]);
        free(converted_input_samples);
    }
    av_frame_free(&input_frame);

    return ret;
}

static int init_output_frame(AVFrame **frame,
                             AVCodecContext *output_codec_context,
                             int frame_size)
{
    int error;

    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate output frame\n");
        return AVERROR_EXIT;
    }

    (*frame)->nb_samples = frame_size;
    (*frame)->ch_layout = output_codec_context->ch_layout;
    (*frame)->format = output_codec_context->sample_fmt;
    (*frame)->sample_rate = output_codec_context->sample_rate;

    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        fprintf(stderr, "Could not allocate output frame samples (error '%s')\n", get_error_text(error));
        av_frame_free(frame);
        return error;
    }
    return 0;
}

static int encode_audio_frame(AVFrame *frame,
                              AVFormatContext *output_format_context,
                              AVCodecContext *output_codec_context,
                              int *data_present)
{
    AVPacket *output_packet = av_packet_alloc();
    if (!output_packet) {
        fprintf(stderr, "Could not allocate output packet\n");
        return AVERROR(ENOMEM);
    }
    int error;

    error = avcodec_send_frame(output_codec_context, frame);
    if (error == AVERROR_EOF) {
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        fprintf(stderr, "Could not send packet for encoding (error '%s')\n", get_error_text(error));
        goto cleanup;
    }

    error = avcodec_receive_packet(output_codec_context, output_packet);
    if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) {
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        fprintf(stderr, "Could not encode frame (error '%s')\n", get_error_text(error));
        goto cleanup;
    } else {
        *data_present = 1;
    }

    if (*data_present && (error = av_write_frame(output_format_context, output_packet)) < 0) {
        fprintf(stderr, "Could not write frame (error '%s')\n", get_error_text(error));
        goto cleanup;
    }

cleanup:
    av_packet_unref(output_packet);
    av_packet_free(&output_packet);
    return error;
}


static int load_encode_and_write(AVAudioFifo *fifo, AVFormatContext *output_format_context,
                                 AVCodecContext *output_codec_context)
{
    AVFrame *output_frame;
    const int frame_size = FFMIN(av_audio_fifo_size(fifo),
                                 output_codec_context->frame_size);
    int data_written;

    if (init_output_frame(&output_frame, output_codec_context, frame_size))
        return AVERROR_EXIT;

    if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }

    if (encode_audio_frame(output_frame, output_format_context, output_codec_context, &data_written)) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }

    av_frame_free(&output_frame);
    return 0;
}

static int write_output_file_trailer(AVFormatContext *output_format_context)
{
    int error;
    if ((error = av_write_trailer(output_format_context)) < 0) {
        fprintf(stderr, "Could not write output file trailer (error '%s')\n", get_error_text(error));
        return error;
    }
    return 0;
}

int main(int argc, char **argv)
{
    AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
    AVCodecContext *input_codec_context = NULL, *output_codec_context = NULL;
    SwrContext *resample_context = NULL;
    AVAudioFifo *fifo = NULL;
    int ret = AVERROR_EXIT;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(1);
    }

    printf("0th - %s\n", argv[0]);
    printf("1st - %s\n", argv[1]);
    printf("2nd - %s\n", argv[2]);


    printf("Debug: Starting to open input file\n");
    if (open_input_file(argv[1], &input_format_context, &input_codec_context)) {
        fprintf(stderr, "Error opening input file\n");
        goto cleanup;
    }

    printf("Debug: Starting to open output file\n");
    if (open_output_file(argv[2], input_codec_context, &output_format_context, &output_codec_context)) {
        fprintf(stderr, "Error opening output file\n");
        goto cleanup;
    }

    printf("Debug: Starting to initialize resampler\n");
    if (init_resampler(input_codec_context, output_codec_context, &resample_context)) {
        fprintf(stderr, "Error initializing resampler\n");
        goto cleanup;
    }

    printf("Debug: Starting to initialize FIFO\n");
    if (init_fifo(&fifo, output_codec_context)) {
        fprintf(stderr, "Error initializing FIFO\n");
        goto cleanup;
    }

    printf("Debug: Starting to write output file header\n");
    if (avformat_write_header(output_format_context, NULL) < 0) {
        fprintf(stderr, "Error writing output file header\n");
        goto cleanup;
    }

    printf("Debug: Starting main conversion loop\n");
    while (1) {
        const int output_frame_size = output_codec_context->frame_size;
        int finished = 0;

        while (av_audio_fifo_size(fifo) < output_frame_size) {
            if (read_decode_convert_and_store(fifo, input_format_context, input_codec_context,
                                              output_codec_context, resample_context, &finished)) {
                fprintf(stderr, "Error in read_decode_convert_and_store\n");
                goto cleanup;
            }

            if (finished) break;
        }

        while (av_audio_fifo_size(fifo) >= output_frame_size ||
               (finished && av_audio_fifo_size(fifo) > 0)) {
            if (load_encode_and_write(fifo, output_format_context, output_codec_context)) {
                fprintf(stderr, "Error in load_encode_and_write\n");
                goto cleanup;
            }
        }

        if (finished) {
            int data_written;
            do {
                data_written = 0;
                if (encode_audio_frame(NULL, output_format_context, output_codec_context, &data_written)) {
                    fprintf(stderr, "Error in encode_audio_frame\n");
                    goto cleanup;
                }
            } while (data_written);
            break;
        }
    }

    if (av_write_trailer(output_format_context) < 0) {
        fprintf(stderr, "Error writing output file trailer\n");
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (fifo) av_audio_fifo_free(fifo);
    if (resample_context) swr_free(&resample_context);
    if (output_codec_context) avcodec_free_context(&output_codec_context);
    if (output_format_context) {
        avio_closep(&output_format_context->pb);
        avformat_free_context(output_format_context);
    }
    if (input_codec_context) avcodec_free_context(&input_codec_context);
    if (input_format_context) avformat_close_input(&input_format_context);

    return ret;
}
