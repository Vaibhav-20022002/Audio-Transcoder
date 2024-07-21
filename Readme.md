# Audio Transcoder to AAC

This project is a simple audio transcoder that converts any audio file to AAC format using the LibAV library. The implementation is done in C.

## Features

- Convert various audio file formats to AAC
- Simple and efficient
- Uses LibAV for encoding and decoding

## Prerequisites

- FFMPEG/LibAV installed on your system
- C compiler (e.g., GCC)

## Installation

1. Clone the repository:

   ```sh
   git clone https://github.com/Vaibhav-20022002/Audio-Transcoder.git
   cd audio-transcoder-to-aac
   ```

2. Install FFMPEG/LibAV:

   ```sh
   sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev ffmpeg
   ```

3. Compile the program:

   ```sh
   gcc -o audio_converter transcode_to_aac.c -lavformat -lavcodec -lavutil -lswresample -lswscale -lm
   ```

## Usage

To convert an audio file to AAC format, use the following command:

```sh
./audio_converter input_file.mp3 output_file.aac
```
