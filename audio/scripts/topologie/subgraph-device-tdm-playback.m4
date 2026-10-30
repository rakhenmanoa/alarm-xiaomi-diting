# Device subgraph for the loudspeaker serial interface as TDM:
#
#   [LOG] -> [TDM EP]
#
# Android drives the CS35L41 pair over TDM, and the vendor voice graph links
# its mailbox and echo canceller to the endpoint over control ports the I2S
# endpoint does not have. The TDM sink is framed exactly like I2S (2 slots of
# 16 bits, long word select inverted, 1 bit delay), so the amplifiers keep
# their I2S configuration. Its configuration is a module config array:
#   PARAM_ID_HW_EP_MF_CFG  48000 Hz, 16 bit, 2 channels, fixed point
#   PARAM_ID_TDM_INTF_CFG  LPAIF, tertiary, internal sync, slots 0-1 of 2,
#                          16 bit slots, long sync, inverted, 1 bit delay
#   PARAM_ID_HW_INTF_CLK_CFG  inverted bit clock (CLOCK_ATTRIBUTE_INVERT_COUPLE_NO,
#                          as q6afe and downstream use for TDM), so data
#                          changes on the edge the amplifiers do not sample
# both addressed to instance 0x6061 (MOD_IID_START + 1, as in XIAOMI-DITING.m4).
# Derived from subgraph-device-i2s-playback.m4 of linux-msm/audioreach-topology.
# SPDX-License-Identifier: BSD-3-Clause
include(`audioreach/audioreach.m4') dnl
include(`util/pcm.m4') dnl
include(`audioreach/subgraph.m4') dnl
include(`audioreach/container.m4') dnl
include(`audioreach/module_log.m4') dnl
include(`util/mixer.m4') dnl
dnl
undefine(`LOG_MODULE_IID') dnl
undefine(`TDM_MODULE_IID') dnl
undefine(`SG_INDEX') dnl
undefine(`CONTAINER_INDEX') dnl
undefine(`MOD_INDEX') dnl

undefine(`DEVICE_PERF_MODE') dnl
undefine(`DEVICE_DIRECTION') dnl
undefine(`CONT_SENARIO_ID') dnl
undefine(`CONT_CAP') dnl
undefine(`CONT_POSITION') dnl
undefine(`DEVICE_DOMAIN_ID') dnl
undefine(`CONT_STACK_SIZE') dnl

define(`DEVICE_PERF_MODE', APM_SG_PERF_MODE_LOW_LATENCY) dnl'
define(`DEVICE_DIRECTION', APM_SUB_GRAPH_DIRECTION_TX) dnl'
define(`CONT_SENARIO_ID', APM_SUB_GRAPH_SID_AUDIO_PLAYBACK) dnl'
define(`CONT_CAP', `ifelse(LPASS_VER_COMPAT, 110000, APM_CONTAINER_TYPE_ID_GC, APM_CONTAINER_CAP_ID_EP)') dnl'
define(`CONT_POSITION', APM_CONT_GRAPH_POS_STREAM)dnl'
define(`DEVICE_DOMAIN_ID', ifdef(`AR_SELECTED_PROC_DOMAIN_ID', `AR_SELECTED_PROC_DOMAIN_ID', `APM_PROC_DOMAIN_ID_ADSP')) dnl'
define(`CONT_STACK_SIZE', 8192) dnl'
dnl
define(`LOG_MODULE_IID', MOD_IID_START) dnl
define(`TDM_MODULE_IID', eval(MOD_IID_START + 1)) dnl
define(`SG_INDEX', 1) dnl
define(`CONTAINER_INDEX', 1) dnl
define(`MOD_INDEX', 1) dnl
dnl
dnl DEVICE_PLAYBACK_ROUTE(stream-index, dai-name, mixer-prefix)
define(`DEVICE_PLAYBACK_ROUTE',
`'
`SectionGraph."NAME_PREFIX.$1 $3 Graph" {'
`        index STR($1)'
`        lines ['
`                "NAME_PREFIX.logger$1, , $3 Audio Mixer"'
`                "NAME_PREFIX.tdm_rx$1, , NAME_PREFIX.logger$1"'
`                "$2 TDM0 Playback, , NAME_PREFIX.tdm_rx$1"'
`        ]'
`}')

AR_SUBGRAPH(SG_INDEX, DEVICE_DAI_ID, SG_IID_START, DEVICE_PERF_MODE, DEVICE_DIRECTION, CONT_SENARIO_ID)
AR_CONTAINER(CONTAINER_INDEX, CONT_IID_START, CONT_CAP, CONT_STACK_SIZE, CONT_POSITION, DEVICE_DOMAIN_ID)

AR_MODULE_LOG(MOD_INDEX, SG_INDEX, CONTAINER_INDEX, LOG_MODULE_IID, 1, 1, 1, 2, 0x000019ab, 1, 0, TDM_MODULE_IID)

SectionVendorTuples."NAME_PREFIX.tdm_rx1_tuples" {
        tokens "audioreach_tokens"

        tuples."word.u32_data" {
                AR_TKN_U32_MODULE_INSTANCE_ID STR(TDM_MODULE_IID)
                AR_TKN_U32_MODULE_ID "0x0700100E"
                AR_TKN_U32_MODULE_MAX_IP_PORTS "1"
                AR_TKN_U32_MODULE_MAX_OP_PORTS "0"
                AR_TKN_U32_MODULE_SRC_OP_PORT_ID "1"
                AR_TKN_U32_MODULE_DST_IN_PORT_ID "0"
                AR_TKN_U32_MODULE_SRC_INSTANCE_ID STR(TDM_MODULE_IID)
                AR_TKN_U32_MODULE_DST_INSTANCE_ID "0"
        }
}

SectionData."NAME_PREFIX.tdm_rx1_data" {
        tuples "NAME_PREFIX.tdm_rx1_tuples"
}

SectionData."NAME_PREFIX.tdm_rx1_cfg" {
        bytes "0x80,0x00,0x00,0x00,0x06,0x10,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x61,0x60,0x00,0x00,0x17,0x10,0x00,0x08,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xbb,0x00,0x00,0x10,0x00,0x02,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x61,0x60,0x00,0x00,0x1b,0x10,0x00,0x08,0x1c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x02,0x00,0x10,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x61,0x60,0x00,0x00,0x3c,0x11,0x00,0x08,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00"
}

SectionWidget."NAME_PREFIX.tdm_rx1" {
        index "1"
        type "aif_in"
        no_pm "true"
        stream_name "DEVICE_NAME TDM0 Playback"
        subseq "10"
        data [
                "NAME_PREFIX.sub_graph1_data"
                "NAME_PREFIX.container1_data"
                "NAME_PREFIX.tdm_rx1_data"
                "NAME_PREFIX.tdm_rx1_cfg"
        ]
}

DEVICE_PLAYBACK_ROUTE(MOD_INDEX, `DEVICE_NAME', `MIXER_PREFIX')
