////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2026 Ripose
//
// This file is part of Memento.
//
// Memento is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License.
//
// Memento is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Memento.  If not, see <https://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#include "quick/coloredimageprovider.h"

#include <utility>

#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrl>

namespace
{

constexpr const char *FILE_SOURCE = "file";
constexpr const char *RESOURCE_SOURCE = "resource";
constexpr const char *SVG_SUFFIX = ".svg";
constexpr const char PATH_SEPARATOR = '/';

/**
 * @brief A decoded request for one image source and tint color.
 */
struct ImageRequest
{
    QString sourcePath;
    QColor color;
};

/**
 * @brief Checks whether a path names an SVG image.
 *
 * @param path The image path to inspect.
 * @return True when the path has an SVG suffix.
 */
[[nodiscard]]
bool isSvgPath(const QString &path)
{
    return path.endsWith(SVG_SUFFIX, Qt::CaseInsensitive);
}

/**
 * @brief Checks that a source path agrees with its declared source type.
 *
 * @param sourceType The caller-provided resource or file type.
 * @param sourcePath The decoded source path.
 * @return True when the path has the declared form.
 */
[[nodiscard]]
bool isValidSource(
    const QString &sourceType,
    const QString &sourcePath)
{
    if (sourceType == RESOURCE_SOURCE)
    {
        return sourcePath.startsWith(":/");
    }
    if (sourceType == FILE_SOURCE)
    {
        return QFileInfo(sourcePath).isAbsolute() &&
            !sourcePath.startsWith(":/");
    }
    return false;
}

/**
 * @brief Decodes a provider ID into an explicitly typed image request.
 *
 * The ID format is source-type/percent-encoded-path/argb-color.
 *
 * @param id The image-provider request ID.
 * @return A valid request, or an empty request when decoding fails.
 */
[[nodiscard]]
ImageRequest parseImageRequest(const QString &id)
{
    const qsizetype firstSeparator = id.indexOf(PATH_SEPARATOR);
    const qsizetype lastSeparator = id.lastIndexOf(PATH_SEPARATOR);
    if (firstSeparator <= 0 || lastSeparator <= firstSeparator)
    {
        return {};
    }

    const QString sourceType = id.first(firstSeparator);
    const QString encodedPath = id.sliced(
        firstSeparator + 1,
        lastSeparator - firstSeparator - 1
    );
    const QString sourcePath = QUrl::fromPercentEncoding(
        encodedPath.toLatin1()
    );
    const QColor color("#" + id.sliced(lastSeparator + 1));
    if (!isValidSource(sourceType, sourcePath) || !color.isValid())
    {
        return {};
    }

    return {sourcePath, color};
}

/**
 * @brief Applies a color to every non-transparent pixel of an image.
 *
 * @param image The source image whose alpha mask will be preserved.
 * @param color The color applied through the source alpha mask.
 * @return A tinted copy of the source image.
 */
[[nodiscard]]
QImage tintImage(QImage image, const QColor &color)
{
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    painter.end();
    return image;
}

/**
 * @brief Renders an SVG source and fills its non-transparent pixels.
 *
 * @param sourcePath The SVG source to render.
 * @param color The fill color.
 * @param size The output size when requested.
 * @param requestedSize The requested output size.
 * @return The filled SVG image, or an empty image when rendering fails.
 */
[[nodiscard]]
QImage renderSvg(
    const QString &sourcePath,
    const QColor &color,
    QSize *size,
    const QSize &requestedSize)
{
    QSvgRenderer renderer(sourcePath);
    if (!renderer.isValid())
    {
        return QImage();
    }

    QSize finalSize = renderer.defaultSize();
    if (requestedSize.isValid())
    {
        finalSize = renderer.defaultSize().scaled(
            requestedSize, Qt::KeepAspectRatio);
    }
    if (size)
    {
        *size = finalSize;
    }

    QImage image(finalSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::LosslessImageRendering);
    renderer.render(&painter);

    painter.end();

    return tintImage(std::move(image), color);
}

/**
 * @brief Renders a raster source and fills its non-transparent pixels.
 *
 * @param sourcePath The raster source to render.
 * @param color The fill color.
 * @param size The output size when requested.
 * @param requestedSize The requested output size.
 * @return The filled raster image, or an empty image when loading fails.
 */
[[nodiscard]]
QImage renderRaster(
    const QString &sourcePath,
    const QColor &color,
    QSize *size,
    const QSize &requestedSize)
{
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
    {
        return QImage();
    }

    QSize finalSize = image.size();
    if (requestedSize.isValid())
    {
        finalSize = image.size().scaled(requestedSize, Qt::KeepAspectRatio);
    }
    if (finalSize.isEmpty())
    {
        return QImage();
    }
    if (image.size() != finalSize)
    {
        image = image.scaled(
            finalSize,
            Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation
        );
    }
    if (size)
    {
        *size = finalSize;
    }

    return tintImage(std::move(image), color);
}

} // namespace

ColoredImageProvider::ColoredImageProvider() :
    QQuickImageProvider(
        QQuickImageProvider::Image,
        QQuickImageProvider::ForceAsynchronousImageLoading
    )
{
}

QImage ColoredImageProvider::requestImage(
    const QString &id, QSize *size, const QSize &requestedSize)
{
    const ImageRequest request = parseImageRequest(id);
    if (request.sourcePath.isEmpty())
    {
        return QImage();
    }

    return isSvgPath(request.sourcePath) ?
        renderSvg(request.sourcePath, request.color, size, requestedSize) :
        renderRaster(request.sourcePath, request.color, size, requestedSize);
}
