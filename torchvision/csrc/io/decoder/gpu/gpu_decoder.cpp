#include "gpu_decoder.h"
#include <c10/cuda/CUDAGuard.h>

/* Set cuda device, create cuda context and initialise the demuxer and decoder.
 */
GPUDecoder::GPUDecoder(std::string src_file, int64_t dev)
    : demuxer(src_file.c_str()), device(dev) {
  at::cuda::CUDAGuard device_guard(device);
  check_for_cuda_errors(
      cuDevicePrimaryCtxRetain(&ctx, device), __LINE__, __FILE__);
  decoder.init(ctx, ffmpeg_to_codec(demuxer.get_video_codec()));
  initialised = true;
}

GPUDecoder::~GPUDecoder() {
  at::cuda::CUDAGuard device_guard(device);
  decoder.release();
  if (initialised) {
    check_for_cuda_errors(
        cuDevicePrimaryCtxRelease(device), __LINE__, __FILE__);
  }
}

/* Fetch a decoded frame tensor after demuxing and decoding.
 */
torch::Tensor GPUDecoder::decode() {
  torch::Tensor frameTensor;
  unsigned long videoBytes = 0;
  uint8_t* video = nullptr;
  at::cuda::CUDAGuard device_guard(device);
  auto options = torch::TensorOptions().dtype(torch::kU8).device(torch::kCUDA);
  torch::Tensor frame = torch::zeros({0}, options);
  do {
    demuxer.demux(&video, &videoBytes);
    decoder.decode(video, videoBytes);
    frame = decoder.fetch_frame();
  } while (frame.numel() == 0 && videoBytes > 0);
  return frame;
}

TORCH_LIBRARY(torchvision, m) {
  m.class_<GPUDecoder>("GPUDecoder")
      .def(torch::init<std::string, int64_t>())
      .def("next", &GPUDecoder::decode);
}
