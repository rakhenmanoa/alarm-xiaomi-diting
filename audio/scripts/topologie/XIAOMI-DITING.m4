# AudioReach topology for the Xiaomi 12T Pro (diting): loudspeakers and the
# two analog microphones (WCD938x over SoundWire into the LPASS TX macro).
# Derived from SM8450-HDK.m4 of linux-msm/audioreach-topology (BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
#
# The two CS35L41 amplifiers sit on the tertiary LPAIF I2S port (Android uses
# it as TDM; mainline AudioReach has no TDM, so it runs as plain I2S here).
# Pins: gpio121 sck, gpio123 ws, gpio124 data1 towards the amplifiers.
include(`audioreach/audioreach.m4')
include(`audioreach/stream-subgraph.m4')
include(`audioreach/device-subgraph.m4')
include(`util/route.m4')
include(`util/mixer.m4')
include(`audioreach/tokens.m4')
dnl
dnl Playback MultiMedia1
STREAM_SG_PCM_ADD(audioreach/subgraph-stream-vol-playback.m4, FRONTEND_DAI_MULTIMEDIA1,
	`S16_LE', 48000, 48000, 2, 2,
	0x00004001, 0x00004001, 0x00006001)
dnl
dnl Capture MultiMedia2
STREAM_SG_PCM_ADD(audioreach/subgraph-stream-capture.m4, FRONTEND_DAI_MULTIMEDIA2,
	`S16_LE', 48000, 48000, 1, 2,
	0x00004002, 0x00004002, 0x00006010)
dnl
dnl Tertiary TDM (framed as I2S) -> speaker amplifiers (data line 1)
DEVICE_SG_ADD(subgraph-device-tdm-playback.m4, `Tertiary', TERTIARY_TDM_RX_0,
	`S16_LE', 48000, 48000, 2, 2,
	LPAIF_INTF_TYPE_LPAIF, I2S_INTF_TYPE_TERTIARY, SD_LINE_IDX_I2S_SD1, DATA_FORMAT_FIXED_POINT,
	0x00004006, 0x00004006, 0x00006060, `TERTIARY_TDM_RX_0')
dnl
dnl WCD938x microphones -> TX macro -> TX codec DMA 3
DEVICE_SG_ADD(audioreach/subgraph-device-codec-dma-capture.m4, `TX_CODEC_DMA_TX_3', TX_CODEC_DMA_TX_3,
	`S16_LE', 48000, 48000, 1, 2,
	LPAIF_INTF_TYPE_RXTX, CODEC_INTF_IDX_TX3, 0, DATA_FORMAT_FIXED_POINT,
	0x00004009, 0x00004009, 0x00006090)

STREAM_DEVICE_PLAYBACK_MIXER(TERTIARY_TDM_RX_0, ``TERTIARY_TDM_RX_0'', ``MultiMedia1'')
STREAM_DEVICE_PLAYBACK_ROUTE(TERTIARY_TDM_RX_0, ``TERTIARY_TDM_RX_0 Audio Mixer'', ``MultiMedia1, stream0.logger1'')

STREAM_DEVICE_CAPTURE_MIXER(FRONTEND_DAI_MULTIMEDIA2, ``TX_CODEC_DMA_TX_3'')
STREAM_DEVICE_CAPTURE_ROUTE(FRONTEND_DAI_MULTIMEDIA2, ``MultiMedia2 Mixer'', ``TX_CODEC_DMA_TX_3, device120.logger1'')
