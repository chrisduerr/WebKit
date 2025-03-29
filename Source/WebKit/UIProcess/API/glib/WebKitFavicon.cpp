/*
 * Copyright (C) 2017 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "WebKitFavicon.h"

#include "WebKitFaviconPrivate.h"
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/WTFGType.h>

struct _WebKitFaviconPrivate {
public:
    GRefPtr<GBytes> bytes;
    int width;
    int height;

    int referenceCount { 1 };
};

WEBKIT_DEFINE_FINAL_TYPE(WebKitFavicon, webkit_favicon, G_TYPE_OBJECT, GObject)

static void webkit_favicon_class_init(WebKitFaviconClass* faviconClass)
{
}

WebKitFavicon* webkitFaviconCreate(GRefPtr<GBytes> bytes, int width, int height)
{
    WebKitFavicon* favicon = WEBKIT_FAVICON(g_object_new(WEBKIT_TYPE_FAVICON, nullptr));

    favicon->priv->width = width;
    favicon->priv->height = height;
    favicon->priv->bytes = bytes;

    return favicon;
}

int webkit_favicon_get_width(WebKitFavicon* favicon)
{
    g_return_val_if_fail(favicon, -1);

    return favicon->priv->width;
}

int webkit_favicon_get_height(WebKitFavicon* favicon)
{
    g_return_val_if_fail(favicon, -1);

    return favicon->priv->height;
}

/**
 * webkit_favicon_get_bytes:
 * @favicon: a #WebKitFavicon
 *
 * Get the #GBytes of @favicon
 *
 * Returns: (transfer none): the #GBytes of @favicon
 */
GBytes* webkit_favicon_get_bytes(WebKitFavicon* favicon)
{
    g_return_val_if_fail(favicon, nullptr);

    return favicon->priv->bytes.get();
}
