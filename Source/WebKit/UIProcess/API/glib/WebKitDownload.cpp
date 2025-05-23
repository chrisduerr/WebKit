/*
 * Copyright (C) 2012, 2014 Igalia S.L.
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
#include "WebKitDownload.h"

#include "APIDownloadClient.h"
#include "DownloadProxy.h"
#include "WebErrors.h"
#include "WebKitDownloadClient.h"
#include "WebKitDownloadPrivate.h"
#include "WebKitPrivate.h"
#include "WebKitURIRequestPrivate.h"
#include "WebKitURIResponsePrivate.h"
#include <WebCore/ResourceResponse.h>
#include <glib/gi18n-lib.h>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/glib/GWeakPtr.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/text/CString.h>

using namespace WebKit;
using namespace WebCore;

/**
 * WebKitDownload:
 *
 * Object used to communicate with the application when downloading.
 *
 * #WebKitDownload carries information about a download request and
 * response, including a #WebKitURIRequest and a #WebKitURIResponse
 * objects. The application may use this object to control the
 * download process, or to simply figure out what is to be downloaded,
 * and handle the download process itself.
 */

enum {
    RECEIVED_DATA,
    FINISHED,
    FAILED,
    DECIDE_DESTINATION,
    CREATED_DESTINATION,

    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_DESTINATION,
    PROP_RESPONSE,
    PROP_ESTIMATED_PROGRESS,
    PROP_ALLOW_OVERWRITE,
    N_PROPERTIES,
};

static std::array<GParamSpec*, N_PROPERTIES> sObjProperties;

struct _WebKitDownloadPrivate {
    ~_WebKitDownloadPrivate()
    {
        if (decideDestinationCallback) {
            g_critical("Bug: application handled WebKitDownload::decide-destination but failed to call webkit_download_set_destination() before the WebKitDownload was destroyed");
            decideDestinationCallback(AllowOverwrite::No, { });
        }
    }

    RefPtr<DownloadProxy> download;

    CompletionHandler<void(AllowOverwrite, String)> decideDestinationCallback;
    GRefPtr<WebKitURIRequest> request;
    GRefPtr<WebKitURIResponse> response;
    GWeakPtr<WebKitWebView> webView;
#if !ENABLE(2022_GLIB_API)
    GUniquePtr<char> destinationURI;
#endif
    GUniquePtr<char> destination;
    guint64 currentSize;
    bool isCancelled;
    GUniquePtr<GTimer> timer;
    gdouble lastProgress;
    gdouble lastElapsed;
    bool allowOverwrite;
};

static std::array<unsigned, LAST_SIGNAL> signals;

WEBKIT_DEFINE_FINAL_TYPE(WebKitDownload, webkit_download, G_TYPE_OBJECT, GObject)

static void webkitDownloadSetProperty(GObject* object, guint propId, const GValue* value, GParamSpec* paramSpec)
{
    WebKitDownload* download = WEBKIT_DOWNLOAD(object);

    switch (propId) {
    case PROP_ALLOW_OVERWRITE:
        webkit_download_set_allow_overwrite(download, g_value_get_boolean(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, paramSpec);
    }
}

static void webkitDownloadGetProperty(GObject* object, guint propId, GValue* value, GParamSpec* paramSpec)
{
    WebKitDownload* download = WEBKIT_DOWNLOAD(object);

    switch (propId) {
    case PROP_DESTINATION:
        g_value_set_string(value, webkit_download_get_destination(download));
        break;
    case PROP_RESPONSE:
        g_value_set_object(value, webkit_download_get_response(download));
        break;
    case PROP_ESTIMATED_PROGRESS:
        g_value_set_double(value, webkit_download_get_estimated_progress(download));
        break;
    case PROP_ALLOW_OVERWRITE:
        g_value_set_boolean(value, webkit_download_get_allow_overwrite(download));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, paramSpec);
    }
}

static void maybeFinishDecideDestination(WebKitDownload* download)
{
    if (auto completionHandler = std::exchange(download->priv->decideDestinationCallback, nullptr))
        completionHandler(download->priv->allowOverwrite ? AllowOverwrite::Yes : AllowOverwrite::No, String::fromUTF8(download->priv->destination.get()));
}

static gboolean webkitDownloadDecideDestination(WebKitDownload* download, const gchar* suggestedFilename)
{
    if (download->priv->destination)
        return FALSE;

    GUniquePtr<char> filename(g_strdelimit(g_strdup(suggestedFilename), G_DIR_SEPARATOR_S, '_'));
    const gchar *downloadsDir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!downloadsDir) {
        // If we don't have XDG user dirs info, set just to HOME.
        downloadsDir = g_get_home_dir();
    }
    download->priv->destination.reset(g_build_filename(downloadsDir, filename.get(), nullptr));
#if !ENABLE(2022_GLIB_API)
    download->priv->destinationURI.reset(g_filename_to_uri(download->priv->destination.get(), nullptr, nullptr));
#endif
    g_object_notify_by_pspec(G_OBJECT(download), sObjProperties[PROP_DESTINATION]);
    maybeFinishDecideDestination(download);
    return TRUE;
}

static void webkit_download_class_init(WebKitDownloadClass* downloadClass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(downloadClass);
    objectClass->set_property = webkitDownloadSetProperty;
    objectClass->get_property = webkitDownloadGetProperty;

#if !ENABLE(2022_GLIB_API)
    downloadClass->decide_destination = webkitDownloadDecideDestination;
#endif

    /**
     * WebKitDownload:destination:
     *
     * The local path to where the download will be saved.
     */
    sObjProperties[PROP_DESTINATION] =
        g_param_spec_string(
            "destination",
            nullptr, nullptr,
            nullptr,
            WEBKIT_PARAM_READABLE);

    /**
     * WebKitDownload:response:
     *
     * The #WebKitURIResponse associated with this download.
     */
    sObjProperties[PROP_RESPONSE] =
        g_param_spec_object(
            "response",
            nullptr, nullptr,
            WEBKIT_TYPE_URI_RESPONSE,
            WEBKIT_PARAM_READABLE);

    /**
     * WebKitDownload:estimated-progress:
     *
     * An estimate of the percent completion for the download operation.
     * This value will range from 0.0 to 1.0. The value is an estimate
     * based on the total number of bytes expected to be received for
     * a download.
     * If you need a more accurate progress information you can connect to
     * #WebKitDownload::received-data signal to track the progress.
     */
    sObjProperties[PROP_ESTIMATED_PROGRESS] =
        g_param_spec_double(
            "estimated-progress",
            nullptr, nullptr,
            0.0, 1.0, 1.0,
            WEBKIT_PARAM_READABLE);

    /**
     * WebKitDownload:allow-overwrite:
     *
     * Whether or not the download is allowed to overwrite an existing file on
     * disk. If this property is %FALSE and the destination already exists,
     * the download will fail.
     *
     * Since: 2.6
     */
    sObjProperties[PROP_ALLOW_OVERWRITE] =
        g_param_spec_boolean(
            "allow-overwrite",
            nullptr, nullptr,
            FALSE,
            WEBKIT_PARAM_READWRITE);

    g_object_class_install_properties(objectClass, N_PROPERTIES, sObjProperties.data());

    /**
     * WebKitDownload::received-data:
     * @download: the #WebKitDownload
     * @data_length: the length of data received in bytes
     *
     * This signal is emitted after response is received,
     * every time new data has been written to the destination. It's
     * useful to know the progress of the download operation.
     */
    signals[RECEIVED_DATA] = g_signal_new(
        "received-data",
        G_TYPE_FROM_CLASS(objectClass),
        G_SIGNAL_RUN_LAST,
        0, nullptr, nullptr,
        g_cclosure_marshal_generic,
        G_TYPE_NONE, 1,
        G_TYPE_UINT64);

    /**
     * WebKitDownload::finished:
     * @download: the #WebKitDownload
     *
     * This signal is emitted when download finishes successfully or due to an error.
     * In case of errors #WebKitDownload::failed signal is emitted before this one.
     */
    signals[FINISHED] =
        g_signal_new("finished",
                     G_TYPE_FROM_CLASS(objectClass),
                     G_SIGNAL_RUN_LAST,
                     0, 0, 0,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);

    /**
     * WebKitDownload::failed:
     * @download: the #WebKitDownload
     * @error: the #GError that was triggered
     *
     * This signal is emitted when an error occurs during the download
     * operation. The given @error, of the domain %WEBKIT_DOWNLOAD_ERROR,
     * contains further details of the failure. If the download is cancelled
     * with webkit_download_cancel(), this signal is emitted with error
     * %WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER. The download operation finishes
     * after an error and #WebKitDownload::finished signal is emitted after this one.
     */
    signals[FAILED] =
        g_signal_new(
            "failed",
            G_TYPE_FROM_CLASS(objectClass),
            G_SIGNAL_RUN_LAST,
            0, 0, 0,
            g_cclosure_marshal_VOID__BOXED,
            G_TYPE_NONE, 1,
            G_TYPE_ERROR | G_SIGNAL_TYPE_STATIC_SCOPE);

    /**
     * WebKitDownload::decide-destination:
     * @download: the #WebKitDownload
     * @suggested_filename: the filename suggested for the download
     *
     * This signal is emitted after response is received to
     * decide a destination for the download using
     * webkit_download_set_destination(). If this signal is not
     * handled, the file will be downloaded to %G_USER_DIRECTORY_DOWNLOAD
     * directory using @suggested_filename.
     *
     * Since 2.40, you may handle this signal asynchronously by
     * returning %TRUE without calling webkit_download_set_destination().
     * This indicates intent to eventually call webkit_download_set_destination().
     * In this case, the download will not proceed until the destination is set
     * or cancelled with webkit_download_cancel().
     *
     * Returns: %TRUE to stop other handlers from being invoked for the event,
     *   or %FALSE to propagate the event further.
     */
    signals[DECIDE_DESTINATION] = g_signal_new(
        "decide-destination",
        G_TYPE_FROM_CLASS(objectClass),
        G_SIGNAL_RUN_LAST,
#if ENABLE(2022_GLIB_API)
        0,
#else
        G_STRUCT_OFFSET(WebKitDownloadClass, decide_destination),
#endif
        g_signal_accumulator_true_handled, nullptr,
        g_cclosure_marshal_generic,
        G_TYPE_BOOLEAN, 1,
        G_TYPE_STRING);

    /**
     * WebKitDownload::created-destination:
     * @download: the #WebKitDownload
     * @destination: the destination
     *
     * This signal is emitted after #WebKitDownload::decide-destination and before
     * #WebKitDownload::received-data to notify that destination file has been
     * created successfully at @destination.
     */
    signals[CREATED_DESTINATION] =
        g_signal_new(
            "created-destination",
            G_TYPE_FROM_CLASS(objectClass),
            G_SIGNAL_RUN_LAST,
            0, 0, 0,
            g_cclosure_marshal_VOID__STRING,
            G_TYPE_NONE, 1,
            G_TYPE_STRING);

#if ENABLE(2022_GLIB_API)
    g_signal_override_class_handler("decide-destination", WEBKIT_TYPE_DOWNLOAD, G_CALLBACK(webkitDownloadDecideDestination));
#endif
}

GRefPtr<WebKitDownload> webkitDownloadCreate(DownloadProxy& downloadProxy, WebKitWebView* webView)
{
    GRefPtr<WebKitDownload> download = adoptGRef(WEBKIT_DOWNLOAD(g_object_new(WEBKIT_TYPE_DOWNLOAD, nullptr)));
    download->priv->download = &downloadProxy;
    download->priv->webView.reset(webView);
    attachDownloadClientToDownload(GRefPtr<WebKitDownload> { download }, downloadProxy);
    return download;
}

static void webkitDownloadUpdateRequest(WebKitDownload* download)
{
    download->priv->request = adoptGRef(webkitURIRequestCreateForResourceRequest(download->priv->download->request()));
}

void webkitDownloadStarted(WebKitDownload* download)
{
    // Update with the final request if needed.
    if (download->priv->request)
        webkitDownloadUpdateRequest(download);
}

void webkitDownloadSetResponse(WebKitDownload* download, WebKitURIResponse* response)
{
    download->priv->response = response;
    g_object_notify_by_pspec(G_OBJECT(download), sObjProperties[PROP_RESPONSE]);
}

bool webkitDownloadIsCancelled(WebKitDownload* download)
{
    return download->priv->isCancelled;
}

void webkitDownloadNotifyProgress(WebKitDownload* download, guint64 bytesReceived)
{
    WebKitDownloadPrivate* priv = download->priv;
    if (priv->isCancelled)
        return;

    if (!download->priv->timer)
        download->priv->timer.reset(g_timer_new());

    priv->currentSize += bytesReceived;
    g_signal_emit(download, signals[RECEIVED_DATA], 0, bytesReceived);

    // Throttle progress notification to not consume high amounts of
    // CPU on fast links, except when the last notification occurred
    // more than 0.016 secs ago (60 FPS), or the last notified progress
    // is passed in 1% or we reached the end.
    gdouble currentElapsed = g_timer_elapsed(priv->timer.get(), 0);
    gdouble currentProgress = webkit_download_get_estimated_progress(download);

    if (priv->lastElapsed
        && priv->lastProgress
        && (currentElapsed - priv->lastElapsed) < 0.016
        && (currentProgress - priv->lastProgress) < 0.01
        && currentProgress < 1.0) {
        return;
    }
    priv->lastElapsed = currentElapsed;
    priv->lastProgress = currentProgress;
    g_object_notify_by_pspec(G_OBJECT(download), sObjProperties[PROP_ESTIMATED_PROGRESS]);
}

void webkitDownloadFailed(WebKitDownload* download, const ResourceError& resourceError)
{
    GUniquePtr<GError> webError(g_error_new_literal(g_quark_from_string(resourceError.domain().utf8().data()),
        toWebKitError(resourceError.errorCode()), resourceError.localizedDescription().utf8().data()));
    if (download->priv->timer)
        g_timer_stop(download->priv->timer.get());

    g_signal_emit(download, signals[FAILED], 0, webError.get());
    g_signal_emit(download, signals[FINISHED], 0, nullptr);
}

void webkitDownloadCancelled(WebKitDownload* download)
{
    // Ignore cancellation for finished downloads.
    if (!WEBKIT_IS_DOWNLOAD(download)) {
        return;
    }

    WebKitDownloadPrivate* priv = download->priv;
    webkitDownloadFailed(download, downloadCancelledByUserError(priv->response ? webkitURIResponseGetResourceResponse(priv->response.get()) : ResourceResponse()));
}

void webkitDownloadFinished(WebKitDownload* download)
{
    if (download->priv->timer)
        g_timer_stop(download->priv->timer.get());
    g_signal_emit(download, signals[FINISHED], 0, nullptr);
}

void webkitDownloadDecideDestinationWithSuggestedFilename(WebKitDownload* download, CString&& suggestedFilename, CompletionHandler<void(AllowOverwrite, String)>&& completionHandler)
{
    if (download->priv->isCancelled) {
        completionHandler(AllowOverwrite::No, { });
        return;
    }

    download->priv->decideDestinationCallback = WTFMove(completionHandler);
    gboolean applicationWillDecideDestination = FALSE;
    g_signal_emit(download, signals[DECIDE_DESTINATION], 0, suggestedFilename.data(), &applicationWillDecideDestination);
    if (!applicationWillDecideDestination)
        maybeFinishDecideDestination(download);
}

void webkitDownloadDestinationCreated(WebKitDownload* download, const String& destinationPath)
{
    if (download->priv->isCancelled)
        return;

#if ENABLE(2022_GLIB_API)
    g_signal_emit(download, signals[CREATED_DESTINATION], 0, destinationPath.utf8().data());
#else
    GUniquePtr<char> destinationURI(g_filename_to_uri(destinationPath.utf8().data(), nullptr, nullptr));
    ASSERT(destinationURI);
    g_signal_emit(download, signals[CREATED_DESTINATION], 0, destinationURI.get());
#endif
}

/**
 * webkit_download_get_request:
 * @download: a #WebKitDownload
 *
 * Retrieves the #WebKitURIRequest object that backs the download
 * process.
 *
 * Returns: (transfer none): the #WebKitURIRequest of @download
 */
WebKitURIRequest* webkit_download_get_request(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), nullptr);

    WebKitDownloadPrivate* priv = download->priv;
    if (!priv->request)
        webkitDownloadUpdateRequest(download);
    return priv->request.get();
}

/**
 * webkit_download_get_destination:
 * @download: a #WebKitDownload
 *
 * Obtains the destination to which the downloaded file will be written.
 *
 * You can connect to #WebKitDownload::created-destination to make
 * sure this method returns a valid destination.
 *
 * Returns: (nullable): the destination or %NULL
 */
const gchar* webkit_download_get_destination(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), nullptr);

#if !ENABLE(2022_GLIB_API)
    if (download->priv->destinationURI)
        return download->priv->destinationURI.get();
#endif
    return download->priv->destination.get();
}

/**
 * webkit_download_set_destination:
 * @download: a #WebKitDownload
 * @destination: the destination
 *
 * Sets the destination to which the downloaded file will be written.
 *
 * This method should be called before the download transfer
 * starts or it will not have any effect on the ongoing download
 * operation. To set the destination using the filename suggested
 * by the server connect to #WebKitDownload::decide-destination
 * signal and call webkit_download_set_destination(). If you want to
 * set a fixed destination that doesn't depend on the suggested
 * filename you can connect to notify::response signal and call
 * webkit_download_set_destination().
 *
 * If #WebKitDownload::decide-destination signal is not handled
 * and destination is not set when the download transfer starts,
 * the file will be saved with the filename suggested by the server in
 * %G_USER_DIRECTORY_DOWNLOAD directory.
 */
void webkit_download_set_destination(WebKitDownload* download, const gchar* destination)
{
    g_return_if_fail(WEBKIT_IS_DOWNLOAD(download));
    g_return_if_fail(destination);
    g_return_if_fail(destination[0] != '\0');
#if ENABLE(2022_GLIB_API)
    g_return_if_fail(g_path_is_absolute(destination));
#else
    g_return_if_fail(g_str_has_prefix(destination, "file://") || g_path_is_absolute(destination));

    GUniquePtr<char> destinationPath;
    if (g_str_has_prefix(destination, "file://")) {
        download->priv->destinationURI.reset(g_strdup(destination));
        destinationPath.reset(g_filename_from_uri(destination, nullptr, nullptr));
        destination = destinationPath.get();
    }
#endif

    if (g_strcmp0(download->priv->destination.get(), destination)) {
#if ENABLE(2022_GLIB_API)
        download->priv->destination.reset(g_strdup(destination));
#else
        if (destinationPath)
            download->priv->destination = WTFMove(destinationPath);
        else
            download->priv->destination.reset(g_strdup(destination));
#endif
        g_object_notify_by_pspec(G_OBJECT(download), sObjProperties[PROP_DESTINATION]);
    }

    maybeFinishDecideDestination(download);
}

/**
 * webkit_download_get_response:
 * @download: a #WebKitDownload
 *
 * Retrieves the #WebKitURIResponse object that backs the download process.
 *
 * Retrieves the #WebKitURIResponse object that backs the download
 * process. This method returns %NULL if called before the response
 * is received from the server. You can connect to notify::response
 * signal to be notified when the response is received.
 *
 * Returns: (transfer none): the #WebKitURIResponse, or %NULL if
 *     the response hasn't been received yet.
 */
WebKitURIResponse* webkit_download_get_response(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), 0);

    return download->priv->response.get();
}

/**
 * webkit_download_cancel:
 * @download: a #WebKitDownload
 *
 * Cancels the download.
 *
 * When the ongoing download
 * operation is effectively cancelled the signal
 * #WebKitDownload::failed is emitted with
 * %WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER error.
 */
void webkit_download_cancel(WebKitDownload* download)
{
    g_return_if_fail(WEBKIT_IS_DOWNLOAD(download));

    maybeFinishDecideDestination(download);

    download->priv->isCancelled = true;
    download->priv->download->cancel([download = Ref { *download->priv->download }] (auto*) {
        download->client().legacyDidCancel(download.get());
    });
}

/**
 * webkit_download_get_estimated_progress:
 * @download: a #WebKitDownload
 *
 * Gets the value of the #WebKitDownload:estimated-progress property.
 * Gets the value of the #WebKitDownload:estimated-progress property.
 * You can monitor the estimated progress of the download operation by
 * connecting to the notify::estimated-progress signal of @download.
 *
 * Returns: an estimate of the of the percent complete for a download
 *     as a range from 0.0 to 1.0.
 */
gdouble webkit_download_get_estimated_progress(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), 0);

    WebKitDownloadPrivate* priv = download->priv;
    if (!priv->response)
        return 0;

    guint64 contentLength = webkit_uri_response_get_content_length(priv->response.get());
    if (!contentLength)
        return 0;

    return static_cast<gdouble>(priv->currentSize) / static_cast<gdouble>(contentLength);
}

/**
 * webkit_download_get_elapsed_time:
 * @download: a #WebKitDownload
 *
 * Gets the elapsed time in seconds, including any fractional part.
 *
 * If the download finished, had an error or was cancelled this is
 * the time between its start and the event.
 *
 * Returns: seconds since the download was started
 */
gdouble webkit_download_get_elapsed_time(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), 0);

    WebKitDownloadPrivate* priv = download->priv;
    if (!priv->timer)
        return 0;

    return g_timer_elapsed(priv->timer.get(), 0);
}

/**
 * webkit_download_get_received_data_length:
 * @download: a #WebKitDownload
 *
 * Gets the length of the data already downloaded for @download.
 *
 * Gets the length of the data already downloaded for @download
 * in bytes.
 *
 * Returns: the amount of bytes already downloaded.
 */
guint64 webkit_download_get_received_data_length(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), 0);

    return download->priv->currentSize;
}

/**
 * webkit_download_get_web_view:
 * @download: a #WebKitDownload
 *
 * Get the #WebKitWebView that initiated the download.
 *
 * Returns: (transfer none): the #WebKitWebView that initiated @download,
 *    or %NULL if @download was not initiated by a #WebKitWebView.
 */
WebKitWebView* webkit_download_get_web_view(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), 0);

    return download->priv->webView.get();
}

/**
 * webkit_download_get_allow_overwrite:
 * @download: a #WebKitDownload
 *
 * Returns the current value of the #WebKitDownload:allow-overwrite property.
 *
 * Returns the current value of the #WebKitDownload:allow-overwrite property,
 * which determines whether the download will overwrite an existing file on
 * disk, or if it will fail if the destination already exists.
 *
 * Returns: the current value of the #WebKitDownload:allow-overwrite property
 *
 * Since: 2.6
 */
gboolean webkit_download_get_allow_overwrite(WebKitDownload* download)
{
    g_return_val_if_fail(WEBKIT_IS_DOWNLOAD(download), FALSE);

    return download->priv->allowOverwrite;
}

/**
 * webkit_download_set_allow_overwrite:
 * @download: a #WebKitDownload
 * @allowed: the new value for the #WebKitDownload:allow-overwrite property
 *
 * Sets the #WebKitDownload:allow-overwrite property.
 *
 * Sets the #WebKitDownload:allow-overwrite property, which determines whether
 * the download may overwrite an existing file on disk, or if it will fail if
 * the destination already exists.
 *
 * Since: 2.6
 */
void webkit_download_set_allow_overwrite(WebKitDownload* download, gboolean allowed)
{
    g_return_if_fail(WEBKIT_IS_DOWNLOAD(download));

    if (allowed == download->priv->allowOverwrite)
        return;

    download->priv->allowOverwrite = allowed;
    g_object_notify_by_pspec(G_OBJECT(download), sObjProperties[PROP_ALLOW_OVERWRITE]);
}
