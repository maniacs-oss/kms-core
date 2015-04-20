/*
 * (C) Copyright 2015 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "kmssdpagent.h"
#include "sdp_utils.h"
#include "kmssdprtpavpmediahandler.h"

#define OBJECT_NAME "rtpavpmediahandler"

GST_DEBUG_CATEGORY_STATIC (kms_sdp_rtp_avp_media_handler_debug_category);
#define GST_CAT_DEFAULT kms_sdp_rtp_avp_media_handler_debug_category

#define parent_class kms_sdp_rtp_avp_media_handler_parent_class

G_DEFINE_TYPE_WITH_CODE (KmsSdpRtpAvpMediaHandler,
    kms_sdp_rtp_avp_media_handler, KMS_TYPE_SDP_RTP_MEDIA_HANDLER,
    GST_DEBUG_CATEGORY_INIT (kms_sdp_rtp_avp_media_handler_debug_category,
        OBJECT_NAME, 0, "debug category for sdp rtp avp media_handler"));

#define KMS_SDP_RTP_AVP_MEDIA_HANDLER_GET_PRIVATE(obj) (  \
  G_TYPE_INSTANCE_GET_PRIVATE (                           \
    (obj),                                                \
    KMS_TYPE_SDP_RTP_AVP_MEDIA_HANDLER,                   \
    KmsSdpRtpAvpMediaHandlerPrivate                       \
  )                                                       \
)

#define SDP_MEDIA_RTP_AVP_PROTO "RTP/AVP"
#define SDP_AUDIO_MEDIA "audio"
#define SDP_VIDEO_MEDIA "video"

#define DEFAULT_RTP_AUDIO_BASE_PAYLOAD 0
#define DEFAULT_RTP_VIDEO_BASE_PAYLOAD 24

/* Table extracted from rfc3551 [6] */
static gchar *rtpmaps[] = {
  /* Payload types (PT) for audio encodings */
  "PCMU/8000/1",
  NULL,                         /* reserved */
  NULL,                         /* reserved */
  "GSM/8000/1",
  "G723/8000/1",
  "DVI4/8000/1",
  "DVI4/16000/1",
  "LPC/8000/1",
  "PCMA/8000/1",
  "G722/8000/1",
  "L16/44100/2",
  "L16/44100/1",
  "QCELP/8000/1",
  "CN/8000/1",
  "MPA/90000",
  "G728/8000/1",
  "DVI4/11025/1",
  "DVI4/22050/1",
  "G729/8000/1",
  NULL,                         /* reserved */
  NULL,                         /* unasigned */
  NULL,                         /* unasigned */
  NULL,                         /* unasigned */
  NULL,                         /* unasigned */

  /* Payload types (PT) for video encodings */
  NULL,                         /* unasigned */
  "CelB/90000",
  "JPEG/90000",
  NULL,                         /* unasigned */
  "nv/90000",
  NULL,                         /* unasigned */
  NULL,                         /* unasigned */
  "H261/90000",
  "MPV/90000",
  "MP2T/90000",
  "H263/90000",
};

typedef struct _KmsSdpRtpMap KmsSdpRtpMap;
struct _KmsSdpRtpMap
{
  guint payload;
  gchar *name;
};

/* TODO: Make these lists configurable */
static KmsSdpRtpMap audio_fmts[] = {
  {98, "OPUS/48000/2"},
  {99, "AMR/8000/1"},
  {0, "PCMU/8000"}
};

static KmsSdpRtpMap video_fmts[] = {
  {96, "H263-1998/90000"},
  {97, "VP8/90000"},
  {100, "MP4V-ES/90000"},
  {101, "H264/90000"}
};

struct _KmsSdpRtpAvpMediaHandlerPrivate
{
  GHashTable *extmaps;
};

static GObject *
kms_sdp_rtp_avp_media_handler_constructor (GType gtype, guint n_properties,
    GObjectConstructParam * properties)
{
  GObjectConstructParam *property;
  gchar const *name;
  GObject *object;
  guint i;

  for (i = 0, property = properties; i < n_properties; ++i, ++property) {
    name = g_param_spec_get_name (property->pspec);
    if (g_strcmp0 (name, "proto") == 0) {
      if (g_value_get_string (property->value) == NULL) {
        /* change G_PARAM_CONSTRUCT_ONLY value */
        g_value_set_string (property->value, SDP_MEDIA_RTP_AVP_PROTO);
      }
    }
  }

  object =
      G_OBJECT_CLASS (parent_class)->constructor (gtype, n_properties,
      properties);

  return object;
}

static gboolean
kms_sdp_rtp_avp_media_handler_add_supported_fmts (GstSDPMedia * media,
    GError ** error)
{
  KmsSdpRtpMap *maps;
  gboolean is_audio;
  guint i, len;

  if (g_strcmp0 (gst_sdp_media_get_media (media), SDP_AUDIO_MEDIA) == 0) {
    len = G_N_ELEMENTS (audio_fmts);
    maps = audio_fmts;
    is_audio = TRUE;
  } else if (g_strcmp0 (gst_sdp_media_get_media (media), SDP_VIDEO_MEDIA) == 0) {
    len = G_N_ELEMENTS (video_fmts);
    maps = video_fmts;
    is_audio = FALSE;
  } else {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Unsuported media '%s'", gst_sdp_media_get_media (media));
    return FALSE;
  }

  for (i = 0; i < len; i++) {
    KmsSdpRtpMap rtpmap;
    gchar *fmt;

    rtpmap = maps[i];

    /* Make some checks for default PTs */
    if (rtpmap.payload >= DEFAULT_RTP_AUDIO_BASE_PAYLOAD &&
        rtpmap.payload <= G_N_ELEMENTS (rtpmaps)) {
      if (rtpmaps[rtpmap.payload] == NULL) {
        g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
            "Trying to use an invalid PT (%d)", rtpmap.payload);
      } else if (is_audio && rtpmap.payload >= DEFAULT_RTP_VIDEO_BASE_PAYLOAD) {
        g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
            "Trying to use a reserved video payload type for audio (%d)",
            rtpmap.payload);
        return FALSE;
      } else if (!is_audio && rtpmap.payload < DEFAULT_RTP_VIDEO_BASE_PAYLOAD) {
        g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
            "Trying to use a reserved audio payload type for video (%d)",
            rtpmap.payload);
        return FALSE;
      } else {
        gchar **codec;
        gboolean ret;

        codec = g_strsplit (rtpmap.name, "/", 0);

        ret = g_str_has_prefix (rtpmaps[rtpmap.payload], codec[0]);
        g_strfreev (codec);

        if (!ret) {
          g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
              "Trying to use a reserved payload (%d) for '%s'",
              rtpmap.payload, rtpmap.name);
          return FALSE;
        }
      }
    }

    fmt = g_strdup_printf ("%u", rtpmap.payload);

    if (gst_sdp_media_add_format (media, fmt) != GST_SDP_OK) {
      g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
          "Can not set format (%u)", rtpmap.payload);
      g_free (fmt);
      return FALSE;
    }

    g_free (fmt);
  }

  return TRUE;
}

static gboolean
kms_sdp_rtp_avp_media_handler_add_extmaps (KmsSdpRtpAvpMediaHandler *
    self, GstSDPMedia * media, GError ** error)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->priv->extmaps);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    guint8 id = GPOINTER_TO_UINT (key);
    const gchar *uri = (const gchar *) value;
    gchar *attr;

    attr = g_strdup_printf ("%" G_GUINT32_FORMAT " %s", id, uri);
    if (gst_sdp_media_add_attribute (media, "extmap", attr) != GST_SDP_OK) {
      g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
          "Can not to set attribute 'rtpmap:%s'", attr);
      g_free (attr);
      return FALSE;
    }
    g_free (attr);
  }

  return TRUE;
}

static gboolean
kms_sdp_rtp_avp_media_handler_add_rtpmap_attrs (GstSDPMedia * media,
    GError ** error)
{
  KmsSdpRtpMap *maps;
  guint i, len;

  if (g_strcmp0 (gst_sdp_media_get_media (media), SDP_AUDIO_MEDIA) == 0) {
    len = G_N_ELEMENTS (audio_fmts);
    maps = audio_fmts;
  } else if (g_strcmp0 (gst_sdp_media_get_media (media), SDP_VIDEO_MEDIA) == 0) {
    len = G_N_ELEMENTS (video_fmts);
    maps = video_fmts;
  } else {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Unsuported media '%s'", gst_sdp_media_get_media (media));
    return FALSE;
  }

  for (i = 0; i < media->fmts->len; i++) {
    gchar *payload, *attr;
    guint pt, j;

    payload = g_array_index (media->fmts, gchar *, i);
    pt = atoi (payload);

    if (pt >= DEFAULT_RTP_AUDIO_BASE_PAYLOAD && pt <= G_N_ELEMENTS (rtpmaps)) {
      /* [rfc4566] rtpmap attribute can be omitted for static payload type  */
      /* numbers so it is completely defined in the RTP Audio/Video profile */
      continue;
    }

    for (j = 0; j < len; j++) {
      if (pt != maps[j].payload) {
        continue;
      }

      attr = g_strdup_printf ("%u %s", maps[j].payload, maps[j].name);

      if (gst_sdp_media_add_attribute (media, "rtpmap", attr) != GST_SDP_OK) {
        g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
            "Can not to set attribute 'rtpmap:%s'", attr);
        g_free (attr);
        return FALSE;
      }

      g_free (attr);
    }
  }

  return TRUE;
}

static GstSDPMedia *
kms_sdp_rtp_avp_media_handler_create_offer (KmsSdpMediaHandler * handler,
    const gchar * media, GError ** error)
{
  GstSDPMedia *m = NULL;

  if (gst_sdp_media_new (&m) != GST_SDP_OK) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Can not create '%s' media", media);
    goto error;
  }

  /* Create m-line */
  if (!KMS_SDP_MEDIA_HANDLER_GET_CLASS (handler)->init_offer (handler, media, m,
          error)) {
    goto error;
  }

  /* Add attributes to m-line */
  if (!KMS_SDP_MEDIA_HANDLER_GET_CLASS (handler)->add_offer_attributes (handler,
          m, error)) {
    goto error;
  }

  return m;

error:
  if (m != NULL) {
    gst_sdp_media_free (m);
  }

  return NULL;
}

static gboolean
encoding_supported (const GstSDPMedia * media, const gchar * enc)
{
  KmsSdpRtpMap *maps;
  guint i, len;

  if (g_strcmp0 (gst_sdp_media_get_media (media), SDP_AUDIO_MEDIA) == 0) {
    len = G_N_ELEMENTS (audio_fmts);
    maps = audio_fmts;
  } else if (g_strcmp0 (gst_sdp_media_get_media (media), SDP_VIDEO_MEDIA) == 0) {
    len = G_N_ELEMENTS (video_fmts);
    maps = video_fmts;
  } else {
    return FALSE;
  }

  for (i = 0; i < len; i++) {
    if (g_str_has_prefix (enc, maps[i].name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
format_supported (const GstSDPMedia * media, const gchar * fmt)
{
  const gchar *val;
  gchar **attrs;
  gboolean ret;

  val = sdp_utils_get_attr_map_value (media, "rtpmap", fmt);

  if (val == NULL) {
    gint pt;

    /* Check if this is a static payload type so they do not need to be */
    /* set in an rtpmap attribute */

    pt = atoi (fmt);
    if (pt >= 0 && pt <= G_N_ELEMENTS (rtpmaps) && rtpmaps[pt] != NULL) {
      return encoding_supported (media, rtpmaps[pt]);
    } else {
      return FALSE;
    }
  }

  attrs = g_strsplit (val, " ", 0);
  ret = encoding_supported (media, attrs[1] /* encoding */ );
  g_strfreev (attrs);

  return ret;
}

static gboolean
    kms_sdp_rtp_avp_media_handler_add_supported_extmaps
    (KmsSdpRtpAvpMediaHandler * self, const GstSDPMedia * offer,
    GstSDPMedia * answer, GError ** error)
{
  guint a;

  for (a = 0;; a++) {
    const gchar *attr;
    GHashTableIter iter;
    gpointer key, value;
    gchar **tokens;
    const gchar *offer_uri;

    attr = gst_sdp_media_get_attribute_val_n (offer, "extmap", a);
    if (attr == NULL) {
      return TRUE;
    }

    tokens = g_strsplit (attr, " ", 0);
    offer_uri = tokens[1];
    if (offer_uri == NULL) {
      g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
          "Offer with wrong extmap '%s'", attr);
      g_strfreev (tokens);
      return FALSE;
    }

    g_hash_table_iter_init (&iter, self->priv->extmaps);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      const gchar *uri = (const gchar *) value;

      if (g_strcmp0 (offer_uri, uri) != 0) {
        continue;
      }

      if (gst_sdp_media_add_attribute (answer, "extmap", attr) != GST_SDP_OK) {
        g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
            "Can not to set attribute 'rtpmap:%s'", attr);
        g_strfreev (tokens);
        return FALSE;
      }
    }

    g_strfreev (tokens);
  }
}

static gboolean
add_supported_rtpmap_attrs (const GstSDPMedia * offer, GstSDPMedia * answer,
    GError ** error)
{
  guint i, len;

  len = gst_sdp_media_formats_len (answer);

  for (i = 0; i < len; i++) {
    const gchar *fmt, *val;

    fmt = gst_sdp_media_get_format (answer, i);
    val = sdp_utils_get_attr_map_value (offer, "rtpmap", fmt);

    if (val == NULL) {
      gint pt;

      /* Check if this is a static payload type so they do not need to be */
      /* set in an rtpmap attribute */

      pt = atoi (fmt);
      if (pt >= 0 && pt <= G_N_ELEMENTS (rtpmaps) && rtpmaps[pt] != NULL) {
        if (encoding_supported (offer, rtpmaps[pt])) {
          /* Static payload do not nee to be set as rtpmap attribute */
          continue;
        } else {
          GST_DEBUG ("No static payload '%s' supported", fmt);
          return FALSE;
        }
      } else {
        GST_DEBUG ("Not 'rtpmap:%s' attribute found in offer", fmt);
        return FALSE;
      }
    }

    if (gst_sdp_media_add_attribute (answer, "rtpmap", val) != GST_SDP_OK) {
      g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
          "Can not add attribute 'rtpmap:%s'", val);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
kms_sdp_rtp_avp_media_handler_can_insert_attribute (KmsSdpMediaHandler *
    handler, const GstSDPMedia * offer, const GstSDPAttribute * attr,
    GstSDPMedia * answer)
{
  if (g_strcmp0 (attr->key, "rtpmap") == 0 ||
      g_strcmp0 (attr->key, "extmap") == 0) {
    /* ignore */
    return FALSE;
  }

  if (!KMS_SDP_MEDIA_HANDLER_CLASS (parent_class)->can_insert_attribute
      (handler, offer, attr, answer)) {
    return FALSE;
  }

  return TRUE;
}

GstSDPMedia *
kms_sdp_rtp_avp_media_handler_create_answer (KmsSdpMediaHandler * handler,
    const GstSDPMedia * offer, GError ** error)
{
  GstSDPMedia *m = NULL;

  if (gst_sdp_media_new (&m) != GST_SDP_OK) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Can not create '%s' media answer", gst_sdp_media_get_media (offer));
    goto error;
  }

  /* Create m-line */
  if (!KMS_SDP_MEDIA_HANDLER_GET_CLASS (handler)->init_answer (handler, offer,
          m, error)) {
    goto error;
  }

  /* Add attributes to m-line */
  if (!KMS_SDP_MEDIA_HANDLER_GET_CLASS (handler)->add_answer_attributes
      (handler, offer, m, error)) {
    goto error;
  }

  if (!KMS_SDP_MEDIA_HANDLER_GET_CLASS (handler)->intersect_sdp_medias (handler,
          offer, m, error)) {
    goto error;
  }

  return m;

error:
  if (m != NULL) {
    gst_sdp_media_free (m);
  }

  return NULL;
}

struct intersect_data
{
  KmsSdpMediaHandler *handler;
  const GstSDPMedia *offer;
  GstSDPMedia *answer;
};

static gboolean
instersect_rtp_avp_media_attr (const GstSDPAttribute * attr, gpointer user_data)
{
  struct intersect_data *data = (struct intersect_data *) user_data;

  if (!KMS_SDP_MEDIA_HANDLER_GET_CLASS (data->handler)->
      can_insert_attribute (data->handler, data->offer, attr, data->answer)) {
    return FALSE;
  }

  if (gst_sdp_media_add_attribute (data->answer, attr->key,
          attr->value) != GST_SDP_OK) {
    GST_WARNING ("Cannot add attribute '%s'", attr->key);
    return FALSE;
  }

  return TRUE;
}

static gboolean
kms_sdp_rtp_avp_media_handler_intersect_sdp_medias (KmsSdpMediaHandler *
    handler, const GstSDPMedia * offer, GstSDPMedia * answer, GError ** error)
{
  struct intersect_data data = {
    .handler = handler,
    .offer = offer,
    .answer = answer
  };

  if (!sdp_utils_intersect_media_attributes (offer,
          instersect_rtp_avp_media_attr, &data)) {
    g_set_error_literal (error, KMS_SDP_AGENT_ERROR,
        SDP_AGENT_UNEXPECTED_ERROR, "Can not intersect media attributes");
    return FALSE;
  }

  return TRUE;
}

static gboolean
kms_sdp_rtp_avp_media_handler_init_offer (KmsSdpMediaHandler * handler,
    const gchar * media, GstSDPMedia * offer, GError ** error)
{
  gchar *proto = NULL;
  gboolean ret = TRUE;

  if (g_strcmp0 (media, SDP_AUDIO_MEDIA) != 0
      && g_strcmp0 (media, SDP_VIDEO_MEDIA) != 0) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_INVALID_MEDIA,
        "Unsupported '%s' media", media);
    ret = FALSE;
    goto end;
  }

  if (gst_sdp_media_set_media (offer, media) != GST_SDP_OK) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Can not set '%s' media", media);
    ret = FALSE;
    goto end;
  }

  g_object_get (handler, "proto", &proto, NULL);

  if (gst_sdp_media_set_proto (offer, proto) != GST_SDP_OK) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Can not set '%s' protocol", SDP_MEDIA_RTP_AVP_PROTO);
    ret = FALSE;
    goto end;
  }

  if (gst_sdp_media_set_port_info (offer, 1, 1) != GST_SDP_OK) {
    g_set_error_literal (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Can not set port");
    ret = FALSE;
  }

end:
  g_free (proto);

  return ret;
}

static gboolean
kms_sdp_rtp_avp_media_handler_add_offer_attributes (KmsSdpMediaHandler *
    handler, GstSDPMedia * offer, GError ** error)
{
  KmsSdpRtpAvpMediaHandler *self = KMS_SDP_RTP_AVP_MEDIA_HANDLER (handler);

  if (!kms_sdp_rtp_avp_media_handler_add_supported_fmts (offer, error)) {
    return FALSE;
  }

  if (!kms_sdp_rtp_avp_media_handler_add_extmaps (self, offer, error)) {
    return FALSE;
  }

  if (!kms_sdp_rtp_avp_media_handler_add_rtpmap_attrs (offer, error)) {
    return FALSE;
  }

  /* Chain up */
  return
      KMS_SDP_MEDIA_HANDLER_CLASS (parent_class)->add_offer_attributes (handler,
      offer, error);
}

static gboolean
kms_sdp_rtp_avp_media_handler_init_answer (KmsSdpMediaHandler * handler,
    const GstSDPMedia * offer, GstSDPMedia * answer, GError ** error)
{
  gchar *proto = NULL;

  if (g_strcmp0 (gst_sdp_media_get_media (offer), SDP_AUDIO_MEDIA) != 0
      && g_strcmp0 (gst_sdp_media_get_media (offer), SDP_VIDEO_MEDIA) != 0) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_INVALID_MEDIA,
        "Unsupported '%s' media", gst_sdp_media_get_media (offer));
    goto error;
  }

  g_object_get (handler, "proto", &proto, NULL);

  if (g_strcmp0 (proto, gst_sdp_media_get_proto (offer)) != 0) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_INVALID_PROTOCOL,
        "Unexpected media protocol '%s'", gst_sdp_media_get_proto (offer));
    goto error;
  }

  if (gst_sdp_media_set_media (answer,
          gst_sdp_media_get_media (offer)) != GST_SDP_OK) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_INVALID_PARAMETER,
        "Can not set '%s' media attribute", gst_sdp_media_get_media (offer));
    goto error;
  }

  if (gst_sdp_media_set_proto (answer, proto) != GST_SDP_OK) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_INVALID_PARAMETER,
        "Can not set proto '%s' attribute", proto);
    goto error;
  }

  g_free (proto);

  return TRUE;

error:
  if (proto != NULL) {
    g_free (proto);
  }

  return FALSE;
}

static gboolean
kms_sdp_rtp_avp_media_handler_add_answer_attributes_impl (KmsSdpMediaHandler *
    handler, const GstSDPMedia * offer, GstSDPMedia * answer, GError ** error)
{
  KmsSdpRtpAvpMediaHandler *self = KMS_SDP_RTP_AVP_MEDIA_HANDLER (handler);
  guint i, len, port;

  len = gst_sdp_media_formats_len (offer);

  if (!KMS_SDP_MEDIA_HANDLER_CLASS (parent_class)->add_answer_attributes
      (handler, offer, answer, error)) {
    return FALSE;
  }

  /* Set only supported media formats in answer */
  for (i = 0; i < len; i++) {
    const gchar *fmt;

    fmt = gst_sdp_media_get_format (offer, i);

    if (!format_supported (offer, fmt)) {
      continue;
    }

    if (gst_sdp_media_add_format (answer, fmt) != GST_SDP_OK) {
      g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
          "Can add format '%s'", fmt);
      return FALSE;
    }
  }

  if (gst_sdp_media_formats_len (answer) > 0) {
    port = 1;
  } else {
    /* Disable media */
    port = 0;
  }

  if (gst_sdp_media_set_port_info (answer, port, 1) != GST_SDP_OK) {
    g_set_error_literal (error, KMS_SDP_AGENT_ERROR,
        SDP_AGENT_INVALID_PARAMETER, "Can not set port attribute");
    return FALSE;
  }

  if (!kms_sdp_rtp_avp_media_handler_add_supported_extmaps (self, offer,
          answer, error)) {
    return FALSE;
  }

  return add_supported_rtpmap_attrs (offer, answer, error);
}

static void
kms_sdp_rtp_avp_media_handler_finalize (GObject * object)
{
  KmsSdpRtpAvpMediaHandler *self = KMS_SDP_RTP_AVP_MEDIA_HANDLER (object);

  GST_DEBUG_OBJECT (self, "finalize");

  g_hash_table_unref (self->priv->extmaps);

  /* chain up */
  G_OBJECT_CLASS (kms_sdp_rtp_avp_media_handler_parent_class)->finalize
      (object);
}

static void
kms_sdp_rtp_avp_media_handler_class_init (KmsSdpRtpAvpMediaHandlerClass * klass)
{
  GObjectClass *gobject_class;
  KmsSdpMediaHandlerClass *handler_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructor = kms_sdp_rtp_avp_media_handler_constructor;
  gobject_class->finalize = kms_sdp_rtp_avp_media_handler_finalize;

  handler_class = KMS_SDP_MEDIA_HANDLER_CLASS (klass);
  handler_class->create_offer = kms_sdp_rtp_avp_media_handler_create_offer;
  handler_class->create_answer = kms_sdp_rtp_avp_media_handler_create_answer;

  handler_class->can_insert_attribute =
      kms_sdp_rtp_avp_media_handler_can_insert_attribute;
  handler_class->intersect_sdp_medias =
      kms_sdp_rtp_avp_media_handler_intersect_sdp_medias;

  handler_class->init_offer = kms_sdp_rtp_avp_media_handler_init_offer;
  handler_class->add_offer_attributes =
      kms_sdp_rtp_avp_media_handler_add_offer_attributes;

  handler_class->init_answer = kms_sdp_rtp_avp_media_handler_init_answer;
  handler_class->add_answer_attributes =
      kms_sdp_rtp_avp_media_handler_add_answer_attributes_impl;

  g_type_class_add_private (klass, sizeof (KmsSdpRtpAvpMediaHandlerPrivate));
}

static void
kms_sdp_rtp_avp_media_handler_init (KmsSdpRtpAvpMediaHandler * self)
{
  self->priv = KMS_SDP_RTP_AVP_MEDIA_HANDLER_GET_PRIVATE (self);

  self->priv->extmaps =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
}

KmsSdpRtpAvpMediaHandler *
kms_sdp_rtp_avp_media_handler_new ()
{
  KmsSdpRtpAvpMediaHandler *handler;

  handler =
      KMS_SDP_RTP_AVP_MEDIA_HANDLER (g_object_new
      (KMS_TYPE_SDP_RTP_AVP_MEDIA_HANDLER, NULL));

  return handler;
}

gboolean
kms_sdp_rtp_avp_media_handler_add_extmap (KmsSdpRtpAvpMediaHandler * self,
    guint8 id, const gchar * uri, GError ** error)
{

  if (g_hash_table_contains (self->priv->extmaps, GUINT_TO_POINTER (id))) {
    g_set_error (error, KMS_SDP_AGENT_ERROR, SDP_AGENT_UNEXPECTED_ERROR,
        "Trying to add existing extmap id '%" G_GUINT32_FORMAT "'", id);
    return FALSE;
  }

  g_hash_table_insert (self->priv->extmaps, GUINT_TO_POINTER (id),
      g_strdup (uri));

  return TRUE;
}
