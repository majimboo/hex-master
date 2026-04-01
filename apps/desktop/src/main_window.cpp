#include "main_window.hpp"

#include "hex_view.hpp"
#include "structure_schema.hpp"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QDesktopServices>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QFormLayout>
#include <QComboBox>
#include <QElapsedTimer>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QGridLayout>
#include <QStackedWidget>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTextBlock>
#include <QTextStream>
#include <QTextEdit>
#include <QTreeWidget>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QWidget>
#include <QMatrix4x4>
#include <QVector3D>
#include <QWheelEvent>

#include <limits>
#include <atomic>
#include <cmath>

namespace {
class SchemaCodeEditor;

struct CompareComputationResult {
    bool ok = false;
    bool canceled = false;
    QString error;
    QString summary;
    QVector<QVariantMap> rows;
    QVector<HexView::HighlightRange> left_highlights;
    QVector<HexView::HighlightRange> right_highlights;
};

enum class ModelIndexType {
    None,
    UInt16,
    UInt32,
};

enum class ModelPrimitiveMode {
    Triangles,
    Lines,
    Points,
};

enum class ModelVertexLayout {
    XYZTogether,
    XYZBlocks,
};

struct ParsedModelPreview {
    QVector<QVector3D> vertices;
    QVector<quint32> indices;
    ModelPrimitiveMode primitive = ModelPrimitiveMode::Triangles;
};

struct ModelScanCandidate {
    quint64 vertex_offset = 0;
    int vertex_stride = 12;
    int vertex_count = 0;
    int score = 0;
    ModelVertexLayout vertex_layout = ModelVertexLayout::XYZTogether;
    quint64 index_offset = 0;
    int index_count = 0;
    ModelIndexType index_type = ModelIndexType::None;
};

struct ModelScanResult {
    bool ok = false;
    bool canceled = false;
    QString error;
    QVector<ModelScanCandidate> candidates;
};

struct SchemaRunResult {
    bool ok = false;
    bool canceled = false;
    QString error;
    QString root_name;
    qint64 covered_bytes = 0;
    qint64 available_from_base = 0;
    qint64 document_size = 0;
    quint64 base_offset = 0;
    StructureSchema::ParsedNode root_node;
};

class SchemaLineNumberArea final : public QWidget {
public:
    explicit SchemaLineNumberArea(QWidget* parent, SchemaCodeEditor* editor) : QWidget(parent), editor_(editor) {}
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    SchemaCodeEditor* editor_;
};

class TreeValueDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto* line_edit = qobject_cast<QLineEdit*>(editor)) {
            QPalette palette = line_edit->palette();
            palette.setColor(QPalette::Base, QColor(43, 108, 176));
            palette.setColor(QPalette::Text, QColor(255, 255, 255));
            palette.setColor(QPalette::Highlight, QColor(173, 216, 255));
            palette.setColor(QPalette::HighlightedText, QColor(24, 32, 40));
            line_edit->setPalette(palette);
            line_edit->setAutoFillBackground(true);
            line_edit->setFrame(false);
            line_edit->setContentsMargins(0, 0, 0, 0);
            line_edit->setTextMargins(0, 0, 0, 0);
            line_edit->setStyleSheet(QStringLiteral(
                "QLineEdit {"
                " margin: 0px;"
                " padding: 0px;"
                " border: 0px;"
                " background: #2b6cb0;"
                " color: white;"
                " selection-background-color: #add8ff;"
                " selection-color: #182028;"
                "}"
            ));
        }
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        editor->setGeometry(option.rect);
    }
};


class EditModeSegmentedControl final : public QWidget {
public:
    explicit EditModeSegmentedControl(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(28);
    }

    void set_actions(QAction* insert_action, QAction* overwrite_action) {
        insert_action_ = insert_action;
        overwrite_action_ = overwrite_action;
        if (insert_action_ != nullptr) {
            QObject::connect(insert_action_, &QAction::changed, this, [this]() { update(); });
            QObject::connect(insert_action_, &QAction::toggled, this, [this]() { update(); });
        }
        if (overwrite_action_ != nullptr) {
            QObject::connect(overwrite_action_, &QAction::changed, this, [this]() { update(); });
            QObject::connect(overwrite_action_, &QAction::toggled, this, [this]() { update(); });
        }
        updateGeometry();
        update();
    }

    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        const int segment_width = qMax(76, qMax(fm.horizontalAdvance(QStringLiteral("Insert")), fm.horizontalAdvance(QStringLiteral("Overwrite"))) + 24);
        return QSize(segment_width * 2, 28);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        const QRect left_rect(0, 0, width() / 2 + 1, height());
        const QRect right_rect(width() / 2, 0, width() - (width() / 2), height());

        QStyleOptionButton left_button;
        init_button_option(&left_button, left_rect, QStringLiteral("Insert"), checked_segment() == 0, hovered_segment_ == 0, pressed_segment_ == 0);
        QStyleOptionButton right_button;
        init_button_option(&right_button, right_rect, QStringLiteral("Overwrite"), checked_segment() == 1, hovered_segment_ == 1, pressed_segment_ == 1);

        painter.save();
        painter.setClipRect(left_rect.adjusted(0, 0, -1, 0));
        style()->drawControl(QStyle::CE_PushButtonBevel, &left_button, &painter, this);
        style()->drawControl(QStyle::CE_PushButtonLabel, &left_button, &painter, this);
        painter.restore();

        painter.save();
        painter.setClipRect(right_rect.adjusted(1, 0, 0, 0));
        style()->drawControl(QStyle::CE_PushButtonBevel, &right_button, &painter, this);
        style()->drawControl(QStyle::CE_PushButtonLabel, &right_button, &painter, this);
        painter.restore();

        const QColor button_face = palette().color(QPalette::Button);
        painter.setPen(QPen(button_face, 2));
        painter.drawLine(width() / 2, 1, width() / 2, height() - 2);

        painter.setPen(palette().color(QPalette::Midlight));
        painter.drawLine(width() / 2, 2, width() / 2, height() - 3);

        if (hasFocus()) {
            QStyleOptionFocusRect focus_option;
            focus_option.initFrom(this);
            focus_option.rect = rect().adjusted(3, 3, -3, -3);
            focus_option.backgroundColor = palette().color(QPalette::Button);
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus_option, &painter, this);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        pressed_segment_ = segment_at(event->pos());
        if (pressed_segment_ >= 0) {
            update();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        hovered_segment_ = segment_at(event->pos());
        if (pressed_segment_ >= 0 && hovered_segment_ != pressed_segment_) {
            update();
        } else {
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        const int released_segment = segment_at(event->pos());
        if (event->button() == Qt::LeftButton && pressed_segment_ >= 0 && released_segment == pressed_segment_) {
            trigger_segment(released_segment);
            event->accept();
        }
        pressed_segment_ = -1;
        update();
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_segment_ = -1;
        update();
        QWidget::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Left) {
            trigger_segment(0);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            trigger_segment(1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            trigger_segment(checked_segment() == 0 ? 1 : 0);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void init_button_option(QStyleOptionButton* option, const QRect& rect, const QString& label, bool checked, bool hovered, bool pressed) const {
        option->initFrom(this);
        option->rect = rect;
        option->text = label;
        option->features = QStyleOptionButton::None;
        option->state |= QStyle::State_Raised;
        if (hovered) {
            option->state |= QStyle::State_MouseOver;
        }
        if (checked) {
            option->state |= QStyle::State_On | QStyle::State_Sunken;
        }
        if (pressed) {
            option->state |= QStyle::State_Sunken;
        }
    }

    int checked_segment() const {
        if (insert_action_ != nullptr && insert_action_->isChecked()) {
            return 0;
        }
        if (overwrite_action_ != nullptr && overwrite_action_->isChecked()) {
            return 1;
        }
        return -1;
    }

    int segment_at(const QPoint& pos) const {
        if (!rect().contains(pos)) {
            return -1;
        }
        return pos.x() < width() / 2 ? 0 : 1;
    }

    void trigger_segment(int segment) {
        QAction* action = segment == 0 ? insert_action_ : overwrite_action_;
        if (action != nullptr && action->isEnabled()) {
            action->trigger();
        }
    }

    QAction* insert_action_ = nullptr;
    QAction* overwrite_action_ = nullptr;
    int hovered_segment_ = -1;
    int pressed_segment_ = -1;
};

QString format_model_offset(quint64 offset) {
    return QStringLiteral("0x%1").arg(offset, 0, 16).toUpper();
}

bool parse_model_offset(const QString& text, quint64* value) {
    if (value == nullptr) {
        return false;
    }

    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    bool ok = false;
    const quint64 parsed = trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
        ? trimmed.mid(2).toULongLong(&ok, 16)
        : trimmed.toULongLong(&ok, 10);
    if (!ok) {
        return false;
    }

    *value = parsed;
    return true;
}

QString model_index_type_label(ModelIndexType type) {
    switch (type) {
    case ModelIndexType::None:
        return QStringLiteral("None");
    case ModelIndexType::UInt16:
        return QStringLiteral("u16");
    case ModelIndexType::UInt32:
        return QStringLiteral("u32");
    }
    return QStringLiteral("Unknown");
}

QString model_vertex_layout_label(ModelVertexLayout layout) {
    switch (layout) {
    case ModelVertexLayout::XYZTogether:
        return QStringLiteral("XYZ together");
    case ModelVertexLayout::XYZBlocks:
        return QStringLiteral("X block, Y block, Z block");
    }
    return QStringLiteral("Unknown");
}

float read_f32_le(const QByteArray& bytes, int offset) {
    float value = 0.0f;
    std::memcpy(&value, bytes.constData() + offset, sizeof(float));
    return value;
}

quint16 read_u16_le(const QByteArray& bytes, int offset) {
    return static_cast<quint16>(static_cast<quint8>(bytes.at(offset)))
        | (static_cast<quint16>(static_cast<quint8>(bytes.at(offset + 1))) << 8);
}

quint32 read_u32_le(const QByteArray& bytes, int offset) {
    return static_cast<quint32>(static_cast<quint8>(bytes.at(offset)))
        | (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 1))) << 8)
        | (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 2))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 3))) << 24);
}

bool load_model_preview(
    const QString& path,
    quint64 vertex_offset,
    int vertex_count,
    ModelVertexLayout vertex_layout,
    int vertex_stride,
    int x_offset,
    int y_offset,
    int z_offset,
    quint64 index_offset,
    int index_count,
    ModelIndexType index_type,
    ModelPrimitiveMode primitive,
    ParsedModelPreview* preview,
    QString* error_message) {
    if (preview == nullptr) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Could not open the source file.");
        }
        return false;
    }

    const qint64 file_size = file.size();
    if (vertex_count <= 0) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Enter a valid vertex count.");
        }
        return false;
    }
    if (vertex_layout == ModelVertexLayout::XYZTogether
        && (vertex_stride < 12
            || x_offset < 0 || y_offset < 0 || z_offset < 0
            || x_offset + 4 > vertex_stride || y_offset + 4 > vertex_stride || z_offset + 4 > vertex_stride)) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Position component offsets must fit inside the vertex stride.");
        }
        return false;
    }

    const quint64 vertex_bytes = vertex_layout == ModelVertexLayout::XYZTogether
        ? static_cast<quint64>(vertex_count) * static_cast<quint64>(vertex_stride)
        : static_cast<quint64>(vertex_count) * 12ULL;
    if (vertex_offset + vertex_bytes > static_cast<quint64>(file_size)) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Vertex buffer range exceeds the file size.");
        }
        return false;
    }

    if (!file.seek(static_cast<qint64>(vertex_offset))) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Could not seek to the vertex buffer.");
        }
        return false;
    }

    const QByteArray vertex_bytes_blob = file.read(static_cast<qint64>(vertex_bytes));
    if (vertex_bytes_blob.size() != static_cast<qint64>(vertex_bytes)) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("Could not read the full vertex buffer.");
        }
        return false;
    }

    preview->vertices.clear();
    preview->indices.clear();
    preview->primitive = primitive;
    preview->vertices.reserve(vertex_count);
    for (int index = 0; index < vertex_count; ++index) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (vertex_layout == ModelVertexLayout::XYZTogether) {
            const int base = index * vertex_stride;
            x = read_f32_le(vertex_bytes_blob, base + x_offset);
            y = read_f32_le(vertex_bytes_blob, base + y_offset);
            z = read_f32_le(vertex_bytes_blob, base + z_offset);
        } else {
            const int component_offset = index * 4;
            const int y_base = vertex_count * 4;
            const int z_base = vertex_count * 8;
            x = read_f32_le(vertex_bytes_blob, component_offset);
            y = read_f32_le(vertex_bytes_blob, y_base + component_offset);
            z = read_f32_le(vertex_bytes_blob, z_base + component_offset);
        }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("The selected layout produced invalid vertex positions.");
            }
            return false;
        }
        preview->vertices.push_back(QVector3D(x, y, z));
    }

    if (index_type != ModelIndexType::None && index_count > 0) {
        const int index_size = index_type == ModelIndexType::UInt16 ? 2 : 4;
        const quint64 total_index_bytes = static_cast<quint64>(index_count) * static_cast<quint64>(index_size);
        if (index_offset + total_index_bytes > static_cast<quint64>(file_size)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Index buffer range exceeds the file size.");
            }
            return false;
        }
        if (!file.seek(static_cast<qint64>(index_offset))) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Could not seek to the index buffer.");
            }
            return false;
        }
        const QByteArray index_bytes_blob = file.read(static_cast<qint64>(total_index_bytes));
        if (index_bytes_blob.size() != static_cast<qint64>(total_index_bytes)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Could not read the full index buffer.");
            }
            return false;
        }

        preview->indices.reserve(index_count);
        for (int i = 0; i < index_count; ++i) {
            quint32 value = index_type == ModelIndexType::UInt16
                ? static_cast<quint32>(read_u16_le(index_bytes_blob, i * 2))
                : read_u32_le(index_bytes_blob, i * 4);
            if (value >= static_cast<quint32>(vertex_count)) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("The selected index buffer references vertices outside the chosen vertex count.");
                }
                return false;
            }
            preview->indices.push_back(value);
        }
    }

    return true;
}

QVector<ModelScanCandidate> scan_model_candidates(
    const QString& path,
    quint64 start_offset,
    quint64 end_offset,
    quint64 step,
    const std::atomic_bool& cancel_requested,
    const std::function<void(quint64, quint64)>& progress_callback) {
    QVector<ModelScanCandidate> results;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return results;
    }

    const quint64 file_size = static_cast<quint64>(file.size());
    const quint64 scan_start = qMin(start_offset, file_size);
    const quint64 scan_end = qMin(end_offset, file_size);
    if (scan_end <= scan_start) {
        return results;
    }

    const QList<int> strides = {12, 16, 20, 24, 28, 32, 36};
    constexpr int kSampleVertices = 96;
    constexpr int kMaxVertices = 2048;
    constexpr int kMaxCandidates = 64;
    constexpr float kCoordinateLimit = 1000000.0f;

    for (quint64 offset = scan_start; offset < scan_end && !cancel_requested.load(); offset += qMax<quint64>(4, step)) {
        progress_callback(offset - scan_start, scan_end - scan_start);

        auto try_add_candidate = [&](ModelScanCandidate candidate, const QVector3D& extent, int valid_vertices) {
            candidate.score = valid_vertices + static_cast<int>(qMin(200.0f, extent.length() * 4.0f));
            const quint64 candidate_vertex_bytes = candidate.vertex_layout == ModelVertexLayout::XYZTogether
                ? static_cast<quint64>(candidate.vertex_count) * static_cast<quint64>(candidate.vertex_stride)
                : static_cast<quint64>(candidate.vertex_count) * 12ULL;
            const quint64 index_offset = candidate.vertex_offset + candidate_vertex_bytes;
            if (index_offset + 18 <= file_size) {
                if (file.seek(static_cast<qint64>(index_offset))) {
                    const QByteArray index_probe = file.read(4096);
                    auto try_indices = [&](ModelIndexType type, int width) {
                        int valid = 0;
                        const int max_indices = qMin(index_probe.size() / width, 2048);
                        for (int i = 0; i < max_indices; ++i) {
                            const quint32 value = type == ModelIndexType::UInt16
                                ? static_cast<quint32>(read_u16_le(index_probe, i * width))
                                : read_u32_le(index_probe, i * width);
                            if (value >= static_cast<quint32>(candidate.vertex_count)) {
                                break;
                            }
                            ++valid;
                        }
                        if (valid >= 9 && valid > candidate.index_count) {
                            candidate.index_offset = index_offset;
                            candidate.index_count = valid - (valid % 3);
                            candidate.index_type = type;
                        }
                    };
                    try_indices(ModelIndexType::UInt16, 2);
                    try_indices(ModelIndexType::UInt32, 4);
                }
            }

            bool duplicate = false;
            for (const ModelScanCandidate& existing : std::as_const(results)) {
                if (existing.vertex_offset == candidate.vertex_offset
                    && existing.vertex_stride == candidate.vertex_stride
                    && existing.vertex_layout == candidate.vertex_layout) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                return;
            }

            results.push_back(candidate);
            std::sort(results.begin(), results.end(), [](const ModelScanCandidate& left, const ModelScanCandidate& right) {
                if (left.score != right.score) {
                    return left.score > right.score;
                }
                return left.vertex_offset < right.vertex_offset;
            });
            if (results.size() > kMaxCandidates) {
                results.resize(kMaxCandidates);
            }
        };

        for (const int stride : strides) {
            const quint64 sample_bytes = static_cast<quint64>(stride) * kSampleVertices;
            if (offset + sample_bytes > file_size) {
                continue;
            }
            if (!file.seek(static_cast<qint64>(offset))) {
                continue;
            }
            const QByteArray sample = file.read(static_cast<qint64>(sample_bytes));
            if (sample.size() != static_cast<qint64>(sample_bytes)) {
                continue;
            }

            int valid_vertices = 0;
            QVector3D min_bounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
            QVector3D max_bounds(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
            for (int i = 0; i < kSampleVertices; ++i) {
                const int base = i * stride;
                const float x = read_f32_le(sample, base);
                const float y = read_f32_le(sample, base + 4);
                const float z = read_f32_le(sample, base + 8);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                    break;
                }
                if (std::abs(x) > kCoordinateLimit || std::abs(y) > kCoordinateLimit || std::abs(z) > kCoordinateLimit) {
                    break;
                }
                min_bounds.setX(qMin(min_bounds.x(), x));
                min_bounds.setY(qMin(min_bounds.y(), y));
                min_bounds.setZ(qMin(min_bounds.z(), z));
                max_bounds.setX(qMax(max_bounds.x(), x));
                max_bounds.setY(qMax(max_bounds.y(), y));
                max_bounds.setZ(qMax(max_bounds.z(), z));
                ++valid_vertices;
            }

            if (valid_vertices < 24) {
                continue;
            }

            const QVector3D extent = max_bounds - min_bounds;
            if (extent.lengthSquared() < 1e-8f) {
                continue;
            }

            int vertex_count = valid_vertices;
            const quint64 probe_vertices = qMin<quint64>(kMaxVertices, (file_size - offset) / static_cast<quint64>(stride));
            if (probe_vertices > static_cast<quint64>(valid_vertices)) {
                if (!file.seek(static_cast<qint64>(offset))) {
                    continue;
                }
                const QByteArray probe = file.read(static_cast<qint64>(probe_vertices * static_cast<quint64>(stride)));
                for (quint64 i = valid_vertices; i < probe_vertices; ++i) {
                    const int base = static_cast<int>(i) * stride;
                    const float x = read_f32_le(probe, base);
                    const float y = read_f32_le(probe, base + 4);
                    const float z = read_f32_le(probe, base + 8);
                    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
                        || std::abs(x) > kCoordinateLimit
                        || std::abs(y) > kCoordinateLimit
                        || std::abs(z) > kCoordinateLimit) {
                        break;
                    }
                    ++vertex_count;
                }
            }

            ModelScanCandidate candidate;
            candidate.vertex_offset = offset;
            candidate.vertex_stride = stride;
            candidate.vertex_count = vertex_count;
            candidate.vertex_layout = ModelVertexLayout::XYZTogether;
            try_add_candidate(candidate, extent, valid_vertices);
        }

        const quint64 planar_sample_bytes = static_cast<quint64>(kSampleVertices) * 12ULL;
        if (offset + planar_sample_bytes <= file_size) {
            if (file.seek(static_cast<qint64>(offset))) {
                const QByteArray sample = file.read(static_cast<qint64>(planar_sample_bytes));
                if (sample.size() == static_cast<qint64>(planar_sample_bytes)) {
                    int valid_vertices = 0;
                    QVector3D min_bounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
                    QVector3D max_bounds(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
                    const int y_base = kSampleVertices * 4;
                    const int z_base = kSampleVertices * 8;
                    for (int i = 0; i < kSampleVertices; ++i) {
                        const int base = i * 4;
                        const float x = read_f32_le(sample, base);
                        const float y = read_f32_le(sample, y_base + base);
                        const float z = read_f32_le(sample, z_base + base);
                        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                            break;
                        }
                        if (std::abs(x) > kCoordinateLimit || std::abs(y) > kCoordinateLimit || std::abs(z) > kCoordinateLimit) {
                            break;
                        }
                        min_bounds.setX(qMin(min_bounds.x(), x));
                        min_bounds.setY(qMin(min_bounds.y(), y));
                        min_bounds.setZ(qMin(min_bounds.z(), z));
                        max_bounds.setX(qMax(max_bounds.x(), x));
                        max_bounds.setY(qMax(max_bounds.y(), y));
                        max_bounds.setZ(qMax(max_bounds.z(), z));
                        ++valid_vertices;
                    }

                    if (valid_vertices >= 24) {
                        const QVector3D extent = max_bounds - min_bounds;
                        if (extent.lengthSquared() >= 1e-8f) {
                            int vertex_count = valid_vertices;
                            const quint64 probe_vertices = qMin<quint64>(kMaxVertices, (file_size - offset) / 12ULL);
                            if (probe_vertices > static_cast<quint64>(valid_vertices)) {
                                if (file.seek(static_cast<qint64>(offset))) {
                                    const QByteArray probe = file.read(static_cast<qint64>(probe_vertices * 12ULL));
                                    const int probe_y_base = static_cast<int>(probe_vertices * 4ULL);
                                    const int probe_z_base = static_cast<int>(probe_vertices * 8ULL);
                                    for (quint64 i = valid_vertices; i < probe_vertices; ++i) {
                                        const int base = static_cast<int>(i) * 4;
                                        const float x = read_f32_le(probe, base);
                                        const float y = read_f32_le(probe, probe_y_base + base);
                                        const float z = read_f32_le(probe, probe_z_base + base);
                                        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
                                            || std::abs(x) > kCoordinateLimit
                                            || std::abs(y) > kCoordinateLimit
                                            || std::abs(z) > kCoordinateLimit) {
                                            break;
                                        }
                                        ++vertex_count;
                                    }
                                }
                            }

                            ModelScanCandidate candidate;
                            candidate.vertex_offset = offset;
                            candidate.vertex_stride = 4;
                            candidate.vertex_count = vertex_count;
                            candidate.vertex_layout = ModelVertexLayout::XYZBlocks;
                            try_add_candidate(candidate, extent, valid_vertices);
                        }
                    }
                }
            }
        }
    }

    progress_callback(scan_end - scan_start, scan_end - scan_start);
    return results;
}

class ModelPreviewWidget final : public QWidget {
public:
    explicit ModelPreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(420, 320);
        setMouseTracking(true);
    }

    void set_model(const ParsedModelPreview& model) {
        model_ = model;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(245, 247, 250));
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (model_.vertices.isEmpty()) {
            painter.setPen(QColor(70, 82, 98));
            painter.drawText(rect().adjusted(24, 24, -24, -24),
                Qt::AlignCenter,
                QStringLiteral("Load or scan a candidate buffer to preview geometry."));
            return;
        }

        QVector3D min_bounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D max_bounds(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        for (const QVector3D& vertex : model_.vertices) {
            min_bounds.setX(qMin(min_bounds.x(), vertex.x()));
            min_bounds.setY(qMin(min_bounds.y(), vertex.y()));
            min_bounds.setZ(qMin(min_bounds.z(), vertex.z()));
            max_bounds.setX(qMax(max_bounds.x(), vertex.x()));
            max_bounds.setY(qMax(max_bounds.y(), vertex.y()));
            max_bounds.setZ(qMax(max_bounds.z(), vertex.z()));
        }

        const QVector3D center = (min_bounds + max_bounds) * 0.5f;
        const QVector3D extent = max_bounds - min_bounds;
        const float radius = qMax(1e-3f, extent.length() * 0.5f);

        QMatrix4x4 transform;
        transform.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        transform.rotate(yaw_, 0.0f, 1.0f, 0.0f);

        QVector<QPointF> projected;
        projected.reserve(model_.vertices.size());
        const float scale = (qMin(width(), height()) * 0.42f * zoom_) / radius;
        for (const QVector3D& vertex : model_.vertices) {
            QVector3D transformed = transform.map(vertex - center);
            const float depth = 1.0f / qMax(0.2f, 3.0f - transformed.z() / radius);
            const QPointF point(
                width() * 0.5 + transformed.x() * scale * depth,
                height() * 0.5 - transformed.y() * scale * depth);
            projected.push_back(point);
        }

        painter.setPen(QPen(QColor(190, 198, 210), 1));
        painter.drawRect(rect().adjusted(10, 10, -10, -10));

        if (model_.primitive == ModelPrimitiveMode::Points) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(46, 109, 180));
            for (const QPointF& point : std::as_const(projected)) {
                painter.drawEllipse(point, 2.2, 2.2);
            }
            return;
        }

        painter.setPen(QPen(QColor(46, 109, 180), 1.1));
        auto draw_segment = [&](quint32 a, quint32 b) {
            if (a < static_cast<quint32>(projected.size()) && b < static_cast<quint32>(projected.size())) {
                painter.drawLine(projected.at(static_cast<int>(a)), projected.at(static_cast<int>(b)));
            }
        };

        if (!model_.indices.isEmpty()) {
            if (model_.primitive == ModelPrimitiveMode::Lines) {
                for (int i = 0; i + 1 < model_.indices.size(); i += 2) {
                    draw_segment(model_.indices.at(i), model_.indices.at(i + 1));
                }
            } else {
                for (int i = 0; i + 2 < model_.indices.size(); i += 3) {
                    draw_segment(model_.indices.at(i), model_.indices.at(i + 1));
                    draw_segment(model_.indices.at(i + 1), model_.indices.at(i + 2));
                    draw_segment(model_.indices.at(i + 2), model_.indices.at(i));
                }
            }
        } else {
            if (model_.primitive == ModelPrimitiveMode::Lines) {
                for (int i = 0; i + 1 < projected.size(); i += 2) {
                    painter.drawLine(projected.at(i), projected.at(i + 1));
                }
            } else {
                for (int i = 0; i + 2 < projected.size(); i += 3) {
                    painter.drawLine(projected.at(i), projected.at(i + 1));
                    painter.drawLine(projected.at(i + 1), projected.at(i + 2));
                    painter.drawLine(projected.at(i + 2), projected.at(i));
                }
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            last_drag_position_ = event->pos();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton)) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        const QPoint delta = event->pos() - last_drag_position_;
        yaw_ += delta.x() * 0.6f;
        pitch_ = qBound(-89.0f, pitch_ + delta.y() * 0.6f, 89.0f);
        last_drag_position_ = event->pos();
        update();
    }

    void wheelEvent(QWheelEvent* event) override {
        const qreal angle = event->angleDelta().y() / 120.0;
        zoom_ = qBound(0.2f, zoom_ * (angle > 0 ? 1.1f : 0.9f), 8.0f);
        update();
        event->accept();
    }

private:
    ParsedModelPreview model_;
    float yaw_ = 24.0f;
    float pitch_ = -18.0f;
    float zoom_ = 1.0f;
    QPoint last_drag_position_;
};

class SchemaCodeEditor final : public QPlainTextEdit {
public:
    explicit SchemaCodeEditor(QWidget* parent = nullptr) : QPlainTextEdit(parent), line_number_area_(new SchemaLineNumberArea(this, this)) {
        connect(this, &QPlainTextEdit::blockCountChanged, this, &SchemaCodeEditor::update_line_number_area_width);
        connect(this, &QPlainTextEdit::updateRequest, this, &SchemaCodeEditor::update_line_number_area);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &SchemaCodeEditor::highlight_current_line);
        update_line_number_area_width(0);
        highlight_current_line();
    }

    int line_number_area_width() const {
        int digits = 1;
        int max_value = qMax(1, blockCount());
        while (max_value >= 10) {
            max_value /= 10;
            ++digits;
        }
        return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void set_error_line(int line_number) {
        error_line_ = line_number;
        highlight_current_line();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect contents = contentsRect();
        line_number_area_->setGeometry(QRect(contents.left(), contents.top(), line_number_area_width(), contents.height()));
    }

private:
    void update_line_number_area_width(int) {
        setViewportMargins(line_number_area_width(), 0, 0, 0);
    }

    void update_line_number_area(const QRect& rect, int dy) {
        if (dy != 0) {
            line_number_area_->scroll(0, dy);
        } else {
            line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());
        }

        if (rect.contains(viewport()->rect())) {
            update_line_number_area_width(0);
        }
    }

    void highlight_current_line() {
        QList<QTextEdit::ExtraSelection> selections;

        QTextEdit::ExtraSelection current_line_selection;
        current_line_selection.format.setBackground(QColor(245, 240, 230));
        current_line_selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        current_line_selection.cursor = textCursor();
        current_line_selection.cursor.clearSelection();
        selections.push_back(current_line_selection);

        if (error_line_ > 0) {
            QTextBlock block = document()->findBlockByNumber(error_line_ - 1);
            if (block.isValid()) {
                QTextCursor error_cursor(block);
                QTextEdit::ExtraSelection error_selection;
                error_selection.cursor = error_cursor;
                error_selection.format.setBackground(QColor(255, 233, 233));
                error_selection.format.setProperty(QTextFormat::FullWidthSelection, true);
                selections.push_back(error_selection);
            }
        }

        setExtraSelections(selections);
    }

    void paint_line_number_area(QPaintEvent* event) {
        QPainter painter(line_number_area_);
        painter.fillRect(event->rect(), QColor(246, 241, 233));
        painter.setPen(QColor(122, 113, 99));

        QTextBlock block = firstVisibleBlock();
        int block_number = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(block_number + 1);
                painter.drawText(0, top, line_number_area_->width() - 6, fontMetrics().height(), Qt::AlignRight, number);
            }

            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++block_number;
        }
    }

    QWidget* line_number_area_;
    int error_line_ = -1;

    friend class SchemaLineNumberArea;
};

QSize SchemaLineNumberArea::sizeHint() const {
    return QSize(editor_->line_number_area_width(), 0);
}

void SchemaLineNumberArea::paintEvent(QPaintEvent* event) {
    editor_->paint_line_number_area(event);
}

class SchemaToolDialog final : public QDialog {
public:
    static constexpr int kNodePtrRole = Qt::UserRole + 10;
    static constexpr int kItemKindRole = Qt::UserRole + 11;
    static constexpr int kChunkStartRole = Qt::UserRole + 12;
    static constexpr int kChunkEndRole = Qt::UserRole + 13;
    static constexpr int kPopulatedRole = Qt::UserRole + 14;
    static constexpr int kItemKindNode = 0;
    static constexpr int kItemKindChunk = 1;
    static constexpr int kChunkSize = 256;

    SchemaToolDialog(HexView* hex_view, QWidget* parent = nullptr)
        : QDialog(parent), hex_view_(hex_view) {
        setWindowTitle(QStringLiteral("Schema Editor"));
        resize(1080, 720);

        auto* root = new QVBoxLayout(this);
        auto* menu_bar = new QMenuBar(this);
        root->setMenuBar(menu_bar);
        auto* file_menu = menu_bar->addMenu(QStringLiteral("&File"));
        auto* new_action = file_menu->addAction(QStringLiteral("&New Schema"));
        new_action->setShortcut(QKeySequence::New);
        auto* open_action = file_menu->addAction(QStringLiteral("&Open Schema..."));
        open_action->setShortcut(QKeySequence::Open);
        recent_menu_ = file_menu->addMenu(QStringLiteral("Open &Recent"));
        file_menu->addSeparator();
        auto* save_action = file_menu->addAction(QStringLiteral("&Save"));
        save_action->setShortcut(QKeySequence::Save);
        auto* save_as_action = file_menu->addAction(QStringLiteral("Save &As..."));
        save_as_action->setShortcut(QKeySequence::SaveAs);
        auto* export_action = file_menu->addAction(QStringLiteral("&Export Parsed Structure..."));
        file_menu->addSeparator();
        auto* close_action = file_menu->addAction(QStringLiteral("&Close"));
        close_action->setShortcut(QKeySequence::Close);
        auto* help_menu = menu_bar->addMenu(QStringLiteral("&Help"));
        auto* syntax_guide_action = help_menu->addAction(QStringLiteral("&Syntax Guide"));

        auto* toolbar = new QHBoxLayout();
        root->addLayout(toolbar);

        auto* run_button = new QPushButton(QStringLiteral("Run"), this);
        base_offset_edit_ = new QLineEdit(this);
        base_offset_edit_->setPlaceholderText(QStringLiteral("0x0"));
        base_offset_edit_->setMaximumWidth(160);
        toolbar->addWidget(new QLabel(QStringLiteral("Base Offset"), this));
        toolbar->addWidget(base_offset_edit_);
        toolbar->addWidget(run_button);
        toolbar->addStretch(1);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        root->addWidget(splitter, 1);

        editor_ = new SchemaCodeEditor(splitter);
        editor_->setPlaceholderText(QStringLiteral("Write a schema DSL here."));
        editor_->setPlainText(StructureSchema::default_schema_template());
        structure_tree_ = new QTreeWidget(splitter);
        structure_tree_->setRootIsDecorated(true);
        structure_tree_->setAlternatingRowColors(true);
        structure_tree_->setUniformRowHeights(true);
        structure_tree_->setColumnCount(5);
        structure_tree_->setHeaderLabels({
            QStringLiteral("Field"),
            QStringLiteral("Type"),
            QStringLiteral("Value"),
            QStringLiteral("Offset"),
            QStringLiteral("Size")});
        structure_tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        structure_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
        structure_tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
        structure_tree_->setStyleSheet(QStringLiteral(
            "QTreeWidget::item:selected {"
            " background: #2b6cb0;"
            " color: white;"
            "}"
        ));
        structure_tree_->header()->setStretchLastSection(false);
        structure_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        structure_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        structure_tree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
        structure_tree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        structure_tree_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({420, 620});

        status_label_ = new QLabel(QStringLiteral("Ready."), this);
        status_label_->setWordWrap(true);
        root->addWidget(status_label_);

        syntax_timer_ = new QTimer(this);
        syntax_timer_->setSingleShot(true);
        syntax_timer_->setInterval(250);

        const QList<QAction*> dialog_actions = {new_action, open_action, save_action, save_as_action, close_action};
        for (QAction* action : dialog_actions) {
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            addAction(action);
        }

        connect(new_action, &QAction::triggered, this, [this]() { new_schema(); });
        connect(open_action, &QAction::triggered, this, [this]() { open_schema(); });
        connect(save_action, &QAction::triggered, this, [this]() { save_schema(); });
        connect(save_as_action, &QAction::triggered, this, [this]() { save_schema_as(); });
        connect(export_action, &QAction::triggered, this, [this]() { export_parsed_structure(); });
        connect(close_action, &QAction::triggered, this, &QDialog::close);
        connect(syntax_guide_action, &QAction::triggered, this, [this]() {
            if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://majimboo.github.io/hex-master/schema-guide.html")))) {
                set_status(QStringLiteral("Failed to open the schema guide link."), true);
            }
        });
        connect(run_button, &QPushButton::clicked, this, [this]() { apply_schema(); });
        connect(editor_, &QPlainTextEdit::textChanged, this, [this]() { syntax_timer_->start(); });
        connect(editor_->document(), &QTextDocument::modificationChanged, this, [this](bool) {
            update_window_title();
        });
        connect(syntax_timer_, &QTimer::timeout, this, [this]() { validate_schema(); });
        connect(structure_tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) { populate_item_children(item); });
        connect(structure_tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_item(); });
        connect(structure_tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_item(); });
        refresh_recent_menu();
    }

    void load_from_settings() {
        QSettings settings;
        current_path_ = settings.value(QStringLiteral("schema/path")).toString();
        recent_files_ = settings.value(QStringLiteral("schema/recentFiles")).toStringList();
        QString schema_text = settings.value(QStringLiteral("schema/text")).toString();
        if (schema_text.trimmed().isEmpty()) {
            schema_text = StructureSchema::default_schema_template();
        }
        editor_->setPlainText(schema_text);
        bool mark_clean = false;
        if (current_path_.isEmpty()) {
            mark_clean = schema_text == StructureSchema::default_schema_template();
        } else if (QFileInfo::exists(current_path_)) {
            QFile file(current_path_);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                mark_clean = QString::fromUtf8(file.readAll()) == schema_text;
            }
        }
        if (mark_clean) {
            mark_schema_clean();
        } else {
            editor_->document()->setModified(true);
            update_window_title();
        }
        const QString offset = settings.value(QStringLiteral("schema/lastOffset"), QStringLiteral("0x0")).toString();
        base_offset_edit_->setText(offset);
        update_window_title();
        refresh_recent_menu();
        validate_schema();
    }

    void save_to_settings() const {
        QSettings settings;
        settings.setValue(QStringLiteral("schema/path"), current_path_);
        settings.setValue(QStringLiteral("schema/recentFiles"), recent_files_);
        settings.setValue(QStringLiteral("schema/text"), editor_->toPlainText());
        settings.setValue(QStringLiteral("schema/lastOffset"), base_offset_edit_->text().trimmed());
    }

    bool request_close() {
        close();
        return !isVisible();
    }

private:
    static QString schema_file_filter() {
        return QStringLiteral("Hex Master Schema (*.hms);;All Files (*)");
    }

    static bool try_parse_offset_text(const QString& text, quint64& value) {
        QString normalized = text.trimmed();
        if (normalized.isEmpty()) {
            return false;
        }
        int base = 10;
        if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            normalized = normalized.mid(2);
            base = 16;
        }
        bool ok = false;
        value = normalized.toULongLong(&ok, base);
        return ok;
    }

    static QString format_size(qint64 bytes) {
        const double value = static_cast<double>(bytes);
        if (bytes >= (1LL << 30)) {
            return QStringLiteral("%1 GiB").arg(value / static_cast<double>(1LL << 30), 0, 'f', 2);
        }
        if (bytes >= (1LL << 20)) {
            return QStringLiteral("%1 MiB").arg(value / static_cast<double>(1LL << 20), 0, 'f', 2);
        }
        if (bytes >= (1LL << 10)) {
            return QStringLiteral("%1 KiB").arg(value / static_cast<double>(1LL << 10), 0, 'f', 2);
        }
        return QStringLiteral("%1 B").arg(bytes);
    }

    void set_status(const QString& text, bool error = false) {
        status_label_->setText(text);
        status_label_->setStyleSheet(error ? QStringLiteral("color: #8b1e1e;") : QString());
    }

    int error_line_from_message(const QString& message) const {
        const QRegularExpression re(QStringLiteral(R"(^Line\s+(\d+):)"));
        const auto match = re.match(message);
        return match.hasMatch() ? match.captured(1).toInt() : -1;
    }

    void validate_schema() {
        StructureSchema::SchemaDefinition schema;
        QString error_message;
        if (StructureSchema::parse_schema(editor_->toPlainText(), schema, &error_message)) {
            editor_->set_error_line(-1);
            set_status(QStringLiteral("Schema is valid."));
        } else {
            editor_->set_error_line(error_line_from_message(error_message));
            set_status(error_message, true);
        }
    }

    QString current_schema_title() const {
        return current_path_.isEmpty() ? QStringLiteral("Untitled Schema") : QFileInfo(current_path_).fileName();
    }

    bool has_unsaved_changes() const {
        return editor_ != nullptr && editor_->document()->isModified();
    }

    void mark_schema_clean() {
        if (editor_ != nullptr) {
            editor_->document()->setModified(false);
        }
        update_window_title();
    }

    bool confirm_discard_schema_changes() {
        if (!has_unsaved_changes()) {
            return true;
        }

        const auto response = QMessageBox::warning(
            this,
            QStringLiteral("Unsaved Schema"),
            QStringLiteral("Save changes to %1 before continuing?").arg(current_schema_title()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);

        if (response == QMessageBox::Cancel) {
            return false;
        }
        if (response == QMessageBox::Save) {
            const QString before_path = current_path_;
            save_schema();
            if (has_unsaved_changes()) {
                if (before_path != current_path_) {
                    update_window_title();
                }
                return false;
            }
        }
        return true;
    }

    void new_schema() {
        if (!confirm_discard_schema_changes()) {
            return;
        }
        editor_->setPlainText(StructureSchema::default_schema_template());
        current_path_.clear();
        base_offset_edit_->setText(QStringLiteral("0x0"));
        mark_schema_clean();
        update_window_title();
        validate_schema();
    }

    void refresh_recent_menu() {
        if (recent_menu_ == nullptr) {
            return;
        }
        recent_menu_->clear();
        int visible_count = 0;
        for (const QString& path : recent_files_) {
            if (!QFileInfo::exists(path)) {
                continue;
            }
            auto* action = recent_menu_->addAction(QFileInfo(path).fileName());
            action->setData(path);
            action->setToolTip(path);
            connect(action, &QAction::triggered, this, [this, action]() {
                open_schema_from_path(action->data().toString());
            });
            ++visible_count;
            if (visible_count >= 10) {
                break;
            }
        }
        if (visible_count == 0) {
            auto* empty_action = recent_menu_->addAction(QStringLiteral("No Recent Schemas"));
            empty_action->setEnabled(false);
        } else {
            recent_menu_->addSeparator();
            auto* clear_action = recent_menu_->addAction(QStringLiteral("Clear Recent"));
            connect(clear_action, &QAction::triggered, this, [this]() {
                recent_files_.clear();
                refresh_recent_menu();
            });
        }
    }

    void add_recent_file(const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        recent_files_.removeAll(path);
        recent_files_.prepend(path);
        while (recent_files_.size() > 10) {
            recent_files_.removeLast();
        }
        refresh_recent_menu();
    }

    bool open_schema_from_path(const QString& path) {
        if (path.isEmpty()) {
            return false;
        }
        if (!confirm_discard_schema_changes()) {
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            recent_files_.removeAll(path);
            refresh_recent_menu();
            set_status(QStringLiteral("Failed to open schema."), true);
            return false;
        }
        editor_->setPlainText(QString::fromUtf8(file.readAll()));
        current_path_ = path;
        add_recent_file(path);
        mark_schema_clean();
        update_window_title();
        validate_schema();
        set_status(QStringLiteral("Opened schema %1").arg(QFileInfo(path).fileName()));
        return true;
    }

    void open_schema() {
        const QString start_dir = current_path_.isEmpty() ? QString() : QFileInfo(current_path_).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Schema"), start_dir, schema_file_filter());
        if (path.isEmpty()) {
            return;
        }
        open_schema_from_path(path);
    }

    bool save_schema_to(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            set_status(QStringLiteral("Failed to save schema."), true);
            return false;
        }
        const QByteArray bytes = editor_->toPlainText().toUtf8();
        if (file.write(bytes) != bytes.size()) {
            set_status(QStringLiteral("Failed to save schema."), true);
            return false;
        }
        current_path_ = path;
        add_recent_file(path);
        mark_schema_clean();
        update_window_title();
        set_status(QStringLiteral("Saved schema %1").arg(QFileInfo(path).fileName()));
        return true;
    }

    void save_schema() {
        if (current_path_.isEmpty()) {
            save_schema_as();
            return;
        }
        save_schema_to(current_path_);
    }

    void save_schema_as() {
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Save Schema As"),
            current_path_.isEmpty() ? QStringLiteral("schema.hms") : QFileInfo(current_path_).fileName(),
            schema_file_filter());
        if (path.isEmpty()) {
            return;
        }
        save_schema_to(path);
    }

    void export_parsed_structure() {
        if (!has_parsed_root_) {
            set_status(QStringLiteral("Run a schema before exporting the parsed structure."), true);
            return;
        }

        const QString suggested_name = current_path_.isEmpty()
            ? QStringLiteral("structure.json")
            : QStringLiteral("%1.json").arg(QFileInfo(current_path_).completeBaseName());
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Export Parsed Structure"),
            suggested_name,
            QStringLiteral("JSON Files (*.json);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            set_status(QStringLiteral("Failed to export the parsed structure."), true);
            return;
        }

        const int total_nodes = count_parsed_nodes(parsed_root_);
        int visited_nodes = 0;
        bool canceled = false;

        QProgressDialog progress_dialog(QStringLiteral("Exporting parsed structure..."), QStringLiteral("Cancel"), 0, 1000, this);
        progress_dialog.setWindowTitle(QStringLiteral("Export Structure"));
        progress_dialog.setWindowModality(Qt::WindowModal);
        progress_dialog.setMinimumDuration(0);
        progress_dialog.setAutoClose(false);
        progress_dialog.setAutoReset(false);
        progress_dialog.setMinimumWidth(480);
        progress_dialog.setValue(0);

        QElapsedTimer progress_timer;
        progress_timer.start();

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "{\n";
        stream << "  \"schemaTitle\": " << json_string_literal(current_schema_title()) << ",\n";
        stream << "  \"exportedAt\": " << json_string_literal(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)) << ",\n";
        stream << "  \"baseOffset\": " << json_string_literal(base_offset_edit_->text().trimmed().isEmpty() ? QStringLiteral("0x0") : base_offset_edit_->text().trimmed()) << ",\n";
        stream << "  \"root\": ";
        write_node_json(stream, parsed_root_, 1, total_nodes, &visited_nodes, &progress_dialog, &progress_timer, &canceled);
        stream << "\n}\n";
        stream.flush();

        if (canceled) {
            file.close();
            file.remove();
            set_status(QStringLiteral("Structure export canceled."), true);
            return;
        }

        if (stream.status() != QTextStream::Ok || file.error() != QFile::NoError) {
            set_status(QStringLiteral("Failed to export the parsed structure."), true);
            return;
        }

        progress_dialog.setValue(1000);
        set_status(QStringLiteral("Exported parsed structure to %1").arg(QFileInfo(path).fileName()));
    }

    static bool is_indexed_array_node(const StructureSchema::ParsedNode& node) {
        if (node.children.isEmpty()) {
            return false;
        }
        for (const auto& child : node.children) {
            if (!child.name.startsWith(QLatin1Char('['))) {
                return false;
            }
        }
        return true;
    }

    static int count_parsed_nodes(const StructureSchema::ParsedNode& node) {
        int total = 1;
        for (const auto& child : node.children) {
            total += count_parsed_nodes(child);
        }
        return total;
    }

    static QString json_string_literal(const QString& value) {
        QString escaped;
        escaped.reserve(value.size() + 8);
        escaped += QLatin1Char('"');
        for (const QChar ch : value) {
            switch (ch.unicode()) {
            case '\"':
                escaped += QStringLiteral("\\\"");
                break;
            case '\\':
                escaped += QStringLiteral("\\\\");
                break;
            case '\b':
                escaped += QStringLiteral("\\b");
                break;
            case '\f':
                escaped += QStringLiteral("\\f");
                break;
            case '\n':
                escaped += QStringLiteral("\\n");
                break;
            case '\r':
                escaped += QStringLiteral("\\r");
                break;
            case '\t':
                escaped += QStringLiteral("\\t");
                break;
            default:
                if (ch.unicode() < 0x20) {
                    escaped += QStringLiteral("\\u%1").arg(static_cast<int>(ch.unicode()), 4, 16, QChar(u'0'));
                } else {
                    escaped += ch;
                }
                break;
            }
        }
        escaped += QLatin1Char('"');
        return escaped;
    }

    static void write_indent(QTextStream& stream, int indent_level) {
        for (int i = 0; i < indent_level; ++i) {
            stream << QStringLiteral("  ");
        }
    }

    static void write_node_json(
        QTextStream& stream,
        const StructureSchema::ParsedNode& node,
        int indent_level,
        int total_nodes,
        int* visited_nodes,
        QProgressDialog* progress_dialog,
        QElapsedTimer* progress_timer,
        bool* canceled) {
        if (canceled != nullptr && *canceled) {
            return;
        }

        if (visited_nodes != nullptr) {
            ++(*visited_nodes);
        }

        if (progress_dialog != nullptr && progress_timer != nullptr && visited_nodes != nullptr) {
            if (progress_timer->elapsed() >= 50 || *visited_nodes >= total_nodes) {
                progress_timer->restart();
                progress_dialog->setValue(total_nodes > 0 ? static_cast<int>((static_cast<qint64>(*visited_nodes) * 1000) / total_nodes) : 1000);
                progress_dialog->setLabelText(QStringLiteral("Exporting parsed structure...\n%1 of %2 nodes")
                                                  .arg(*visited_nodes)
                                                  .arg(total_nodes));
                QApplication::processEvents();
                if (progress_dialog->wasCanceled() && canceled != nullptr) {
                    *canceled = true;
                    return;
                }
            }
        }

        stream << "{\n";
        write_indent(stream, indent_level + 1);
        stream << "\"name\": " << json_string_literal(node.name) << ",\n";
        write_indent(stream, indent_level + 1);
        stream << "\"type\": " << json_string_literal(node.type_name) << ",\n";
        write_indent(stream, indent_level + 1);
        stream << "\"value\": " << json_string_literal(node.value) << ",\n";
        write_indent(stream, indent_level + 1);
        stream << "\"offset\": " << json_string_literal(QStringLiteral("0x%1").arg(node.offset, 0, 16).toUpper()) << ",\n";
        write_indent(stream, indent_level + 1);
        stream << "\"offsetDecimal\": " << json_string_literal(QString::number(node.offset)) << ",\n";
        write_indent(stream, indent_level + 1);
        stream << "\"size\": " << json_string_literal(QString::number(node.size));

        if (!node.children.isEmpty()) {
            stream << ",\n";
            write_indent(stream, indent_level + 1);
            stream << "\"children\": [\n";
            for (int index = 0; index < node.children.size(); ++index) {
                write_indent(stream, indent_level + 2);
                write_node_json(stream, node.children.at(index), indent_level + 2, total_nodes, visited_nodes, progress_dialog, progress_timer, canceled);
                if (canceled != nullptr && *canceled) {
                    return;
                }
                if (index + 1 < node.children.size()) {
                    stream << ",";
                }
                stream << "\n";
            }
            write_indent(stream, indent_level + 1);
            stream << "]\n";
            write_indent(stream, indent_level);
            stream << "}";
            return;
        }

        stream << "\n";
        write_indent(stream, indent_level);
        stream << "}";
    }

    static QVariant node_ptr_variant(const StructureSchema::ParsedNode* node) {
        return QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(node));
    }

    static const StructureSchema::ParsedNode* item_node(const QTreeWidgetItem* item) {
        return reinterpret_cast<const StructureSchema::ParsedNode*>(static_cast<quintptr>(item->data(0, kNodePtrRole).toULongLong()));
    }

    QString display_value_for_node(const StructureSchema::ParsedNode& node) const {
        return node.value;
    }

    QTreeWidgetItem* append_node(QTreeWidgetItem* parent, const StructureSchema::ParsedNode& node) {
        const QString offset_value = QStringLiteral("0x%1").arg(node.offset, 0, 16).toUpper();
        const QString size_value = QString::number(node.size);
        const QString display_value = display_value_for_node(node);
        auto* item = parent == nullptr
            ? new QTreeWidgetItem(structure_tree_, QStringList{node.name, node.type_name, display_value, offset_value, size_value})
            : new QTreeWidgetItem(parent, QStringList{node.name, node.type_name, display_value, offset_value, size_value});
        item->setData(0, Qt::UserRole, node.offset);
        item->setData(1, Qt::UserRole, node.size);
        item->setData(0, kNodePtrRole, node_ptr_variant(&node));
        item->setData(0, kItemKindRole, kItemKindNode);
        item->setData(0, kPopulatedRole, false);
        if (!node.children.isEmpty()) {
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
        return item;
    }

    void append_chunk_item(QTreeWidgetItem* parent, const StructureSchema::ParsedNode& node, int start_index, int end_index) {
        const QString label = QStringLiteral("[%1..%2]").arg(start_index).arg(end_index);
        auto* item = new QTreeWidgetItem(parent, QStringList{
            label,
            QStringLiteral("Array Slice"),
            QStringLiteral("%1 item(s)").arg(end_index - start_index + 1),
            QString(),
            QString()});
        item->setData(0, kNodePtrRole, node_ptr_variant(&node));
        item->setData(0, kItemKindRole, kItemKindChunk);
        item->setData(0, kChunkStartRole, start_index);
        item->setData(0, kChunkEndRole, end_index);
        item->setData(0, kPopulatedRole, false);
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }

    void populate_item_children(QTreeWidgetItem* item) {
        if (item == nullptr || item->data(0, kPopulatedRole).toBool()) {
            return;
        }

        const auto* node = item_node(item);
        if (node == nullptr) {
            item->setData(0, kPopulatedRole, true);
            return;
        }

        const int item_kind = item->data(0, kItemKindRole).toInt();
        if (item_kind == kItemKindChunk) {
            const int start_index = item->data(0, kChunkStartRole).toInt();
            const int end_index = item->data(0, kChunkEndRole).toInt();
            for (int index = start_index; index <= end_index && index < node->children.size(); ++index) {
                append_node(item, node->children.at(index));
            }
            item->setData(0, kPopulatedRole, true);
            return;
        }

        if (is_indexed_array_node(*node) && node->children.size() > kChunkSize) {
            for (int start = 0; start < node->children.size(); start += kChunkSize) {
                const int end = qMin(start + kChunkSize - 1, node->children.size() - 1);
                append_chunk_item(item, *node, start, end);
            }
        } else {
            for (const auto& child : node->children) {
                append_node(item, child);
            }
        }
        item->setData(0, kPopulatedRole, true);
    }

    void update_window_title() {
        const QString suffix = has_unsaved_changes() ? QStringLiteral(" *") : QString();
        if (current_path_.isEmpty()) {
            setWindowTitle(QStringLiteral("Schema Editor%1").arg(suffix));
            return;
        }
        setWindowTitle(QStringLiteral("Schema Editor - %1%2").arg(QFileInfo(current_path_).fileName(), suffix));
    }

    void apply_schema() {
        if (hex_view_ == nullptr || !hex_view_->has_document()) {
            set_status(QStringLiteral("Open a file before applying a schema."), true);
            return;
        }

        StructureSchema::SchemaDefinition schema;
        QString error_message;
        if (!StructureSchema::parse_schema(editor_->toPlainText(), schema, &error_message)) {
            editor_->set_error_line(error_line_from_message(error_message));
            set_status(error_message, true);
            return;
        }

        quint64 base_offset = 0;
        if (!try_parse_offset_text(base_offset_edit_->text().trimmed().isEmpty() ? QStringLiteral("0x%1").arg(hex_view_->caret_offset(), 0, 16) : base_offset_edit_->text(), base_offset)) {
            set_status(QStringLiteral("Invalid base offset."), true);
            return;
        }

        const qint64 document_size = hex_view_->document_size();
        const qint64 base_offset_signed = static_cast<qint64>(base_offset);
        const qint64 available_from_base = qMax<qint64>(0, document_size - base_offset_signed);

        QProgressDialog progress_dialog(QStringLiteral("Applying schema..."), QStringLiteral("Cancel"), 0, 1000, this);
        progress_dialog.setWindowTitle(QStringLiteral("Schema Run"));
        progress_dialog.setWindowModality(Qt::WindowModal);
        progress_dialog.setMinimumDuration(0);
        progress_dialog.setAutoClose(false);
        progress_dialog.setAutoReset(false);
        progress_dialog.setMinimumWidth(480);
        progress_dialog.setValue(0);
        QElapsedTimer progress_timer;
        progress_timer.start();
        StructureSchema::ParsedNode root_node;
        if (!StructureSchema::evaluate_schema(
                schema,
                base_offset_signed,
                document_size,
                [this](qint64 start, qint64 length) { return hex_view_->read_bytes(start, length); },
                root_node,
                [&](qint64, qint64 covered_bytes) {
                    if (progress_timer.elapsed() < 50 && covered_bytes < available_from_base) {
                        return !progress_dialog.wasCanceled();
                    }
                    progress_timer.restart();
                    const int value = available_from_base > 0
                        ? static_cast<int>(qBound<qint64>(0, (covered_bytes * 1000) / available_from_base, 1000LL))
                        : 1000;
                    progress_dialog.setLabelText(QStringLiteral("Applying schema...\n%1 of %2")
                                                     .arg(format_size(qMax<qint64>(0, covered_bytes)))
                                                     .arg(format_size(available_from_base)));
                    progress_dialog.setValue(value);
                    QApplication::processEvents();
                    return !progress_dialog.wasCanceled();
                },
                &error_message)) {
            progress_dialog.cancel();
            structure_tree_->clear();
            editor_->set_error_line(error_line_from_message(error_message));
            set_status(error_message, true);
            return;
        }

        progress_dialog.setValue(1000);

        editor_->set_error_line(-1);
        parsed_root_ = std::move(root_node);
        has_parsed_root_ = true;
        structure_tree_->clear();
        structure_tree_->setUpdatesEnabled(false);
        auto* root_item = append_node(nullptr, parsed_root_);
        populate_item_children(root_item);
        root_item->setExpanded(true);
        structure_tree_->setUpdatesEnabled(true);

        const qint64 covered_bytes = qMax<qint64>(0, parsed_root_.size);
        const qint64 trailing_bytes = qMax<qint64>(0, available_from_base - covered_bytes);
        const double remaining_percent = available_from_base > 0
            ? (100.0 * static_cast<double>(covered_bytes) / static_cast<double>(available_from_base))
            : 100.0;
        const double file_percent = document_size > 0
            ? (100.0 * static_cast<double>(covered_bytes) / static_cast<double>(document_size))
            : 100.0;
        const QString coverage_note = trailing_bytes == 0
            ? QStringLiteral("full coverage from base offset")
            : QStringLiteral("%1 trailing byte(s) not covered").arg(trailing_bytes);

        set_status(QStringLiteral(
                       "Applied schema `%1` at 0x%2. Covered %3 of %4 from base offset (%5% of remaining file, %6% of total file), %7.")
                       .arg(schema.root_name)
                       .arg(base_offset, 0, 16)
                       .arg(format_size(covered_bytes))
                       .arg(format_size(available_from_base))
                       .arg(remaining_percent, 0, 'f', trailing_bytes == 0 ? 0 : 1)
                       .arg(file_percent, 0, 'f', 1)
                       .arg(coverage_note));
    }

    void activate_item() {
        if (hex_view_ == nullptr) {
            return;
        }
        QTreeWidgetItem* item = structure_tree_->currentItem();
        if (item == nullptr) {
            return;
        }
        const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
        const qint64 size = item->data(1, Qt::UserRole).toLongLong();
        if (size > 0) {
            hex_view_->select_range(offset, size);
        }
    }

    void closeEvent(QCloseEvent* event) override {
        if (!confirm_discard_schema_changes()) {
            event->ignore();
            return;
        }
        save_to_settings();
        QDialog::closeEvent(event);
    }

    HexView* hex_view_ = nullptr;
    SchemaCodeEditor* editor_ = nullptr;
    QTreeWidget* structure_tree_ = nullptr;
    QLineEdit* base_offset_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* syntax_timer_ = nullptr;
    QMenu* recent_menu_ = nullptr;
    QString current_path_;
    QStringList recent_files_;
    StructureSchema::ParsedNode parsed_root_;
    bool has_parsed_root_ = false;
};

class CompareToolDialog final : public QDialog {
public:
    explicit CompareToolDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Compare Files"));
        resize(1380, 860);

        auto* root = new QVBoxLayout(this);

        auto* path_layout = new QGridLayout();
        path_layout->setContentsMargins(0, 0, 0, 0);
        path_layout->setHorizontalSpacing(8);
        path_layout->setVerticalSpacing(8);
        root->addLayout(path_layout);

        source_edit_ = new QLineEdit(this);
        target_edit_ = new QLineEdit(this);
        auto* browse_source = new QPushButton(QStringLiteral("Browse..."), this);
        auto* browse_target = new QPushButton(QStringLiteral("Browse..."), this);
        auto* swap_button = new QPushButton(QStringLiteral("Swap"), this);
        compare_button_ = new QPushButton(QStringLiteral("Compare"), this);
        compare_button_->setDefault(true);

        path_layout->addWidget(new QLabel(QStringLiteral("Source File"), this), 0, 0);
        path_layout->addWidget(source_edit_, 0, 1);
        path_layout->addWidget(browse_source, 0, 2);
        path_layout->addWidget(new QLabel(QStringLiteral("Target File"), this), 1, 0);
        path_layout->addWidget(target_edit_, 1, 1);
        path_layout->addWidget(browse_target, 1, 2);
        path_layout->setColumnStretch(1, 1);

        auto* actions_layout = new QHBoxLayout();
        actions_layout->setContentsMargins(0, 4, 0, 0);
        actions_layout->setSpacing(8);
        actions_layout->addStretch(1);
        actions_layout->addWidget(swap_button);
        actions_layout->addWidget(compare_button_);
        actions_layout->addStretch(1);
        root->addLayout(actions_layout);

        auto* summary_bar = new QWidget(this);
        summary_bar->setObjectName(QStringLiteral("compareSummaryBar"));
        summary_bar->setStyleSheet(QStringLiteral(
            "#compareSummaryBar {"
            " background: #eef3f8;"
            " border: 1px solid #d3dce6;"
            " border-radius: 8px;"
            " }"
            "#compareSummaryBar QLabel#comparePathLabel {"
            " font-weight: 600;"
            " color: #243447;"
            " }"
            "#compareSummaryBar QLabel#compareSummaryLabel {"
            " color: #486070;"
            " }"));
        auto* summary_layout = new QHBoxLayout(summary_bar);
        summary_layout->setContentsMargins(12, 8, 12, 8);
        summary_layout->setSpacing(12);
        summary_label_ = new QLabel(QStringLiteral("Choose two files and run compare."), summary_bar);
        summary_label_->setObjectName(QStringLiteral("compareSummaryLabel"));
        summary_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        previous_button_ = new QPushButton(QStringLiteral("Previous Difference"), summary_bar);
        next_button_ = new QPushButton(QStringLiteral("Next Difference"), summary_bar);
        summary_layout->addWidget(summary_label_, 1);
        summary_layout->addWidget(previous_button_);
        summary_layout->addWidget(next_button_);
        root->addWidget(summary_bar);

        vertical_splitter_ = new QSplitter(Qt::Vertical, this);
        root->addWidget(vertical_splitter_, 1);

        auto* views_widget = new QWidget(vertical_splitter_);
        auto* views_layout = new QHBoxLayout(views_widget);
        views_layout->setContentsMargins(0, 0, 0, 0);
        views_layout->setSpacing(0);
        auto* left_container = new QWidget(views_widget);
        auto* left_container_layout = new QVBoxLayout(left_container);
        left_container_layout->setContentsMargins(0, 0, 0, 0);
        left_container_layout->setSpacing(0);
        left_label_ = new QLabel(QStringLiteral("Source"), left_container);
        left_label_->setObjectName(QStringLiteral("comparePathLabel"));
        left_label_->setStyleSheet(QStringLiteral(
            "QLabel {"
            " background: #eef3f8;"
            " border: 1px solid #d3dce6;"
            " border-bottom: 0;"
            " padding: 6px 10px;"
            " font-weight: 600;"
            " color: #243447;"
            " }"));
        left_container_layout->addWidget(left_label_);
        left_view_ = new HexView(left_container);
        left_container_layout->addWidget(left_view_, 1);

        auto* right_container = new QWidget(views_widget);
        auto* right_container_layout = new QVBoxLayout(right_container);
        right_container_layout->setContentsMargins(0, 0, 0, 0);
        right_container_layout->setSpacing(0);
        right_label_ = new QLabel(QStringLiteral("Target"), right_container);
        right_label_->setObjectName(QStringLiteral("comparePathLabel"));
        right_label_->setStyleSheet(QStringLiteral(
            "QLabel {"
            " background: #eef3f8;"
            " border: 1px solid #d3dce6;"
            " border-bottom: 0;"
            " padding: 6px 10px;"
            " font-weight: 600;"
            " color: #243447;"
            " }"));
        right_container_layout->addWidget(right_label_);
        right_view_ = new HexView(right_container);
        right_container_layout->addWidget(right_view_, 1);

        view_splitter_ = new QSplitter(Qt::Horizontal, views_widget);
        view_splitter_->addWidget(left_container);
        view_splitter_->addWidget(right_container);
        view_splitter_->setStretchFactor(0, 1);
        view_splitter_->setStretchFactor(1, 1);
        view_splitter_->setSizes({650, 650});
        views_layout->addWidget(view_splitter_);

        results_tree_ = new QTreeWidget(vertical_splitter_);
        results_tree_->setRootIsDecorated(false);
        results_tree_->setAlternatingRowColors(true);
        results_tree_->setUniformRowHeights(true);
        results_tree_->setColumnCount(4);
        results_tree_->setHeaderLabels({QStringLiteral("Offset"), QStringLiteral("Status"), QStringLiteral("Length"), QStringLiteral("Preview")});
        results_tree_->header()->setStretchLastSection(true);
        results_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        results_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        results_tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        vertical_splitter_->setStretchFactor(0, 1);
        vertical_splitter_->setStretchFactor(1, 0);
        vertical_splitter_->setSizes({620, 220});

        status_label_ = new QLabel(QStringLiteral("Ready."), this);
        status_label_->setWordWrap(true);
        root->addWidget(status_label_);

        previous_button_->setEnabled(false);
        next_button_->setEnabled(false);

        connect(browse_source, &QPushButton::clicked, this, [this]() { browse_for_file(source_edit_, QStringLiteral("Open Source File")); });
        connect(browse_target, &QPushButton::clicked, this, [this]() { browse_for_file(target_edit_, QStringLiteral("Open Target File")); });
        connect(source_edit_, &QLineEdit::textChanged, this, [this](const QString&) { update_summary_labels(); });
        connect(target_edit_, &QLineEdit::textChanged, this, [this](const QString&) { update_summary_labels(); });
        connect(swap_button, &QPushButton::clicked, this, [this]() {
            const QString source = source_edit_->text();
            source_edit_->setText(target_edit_->text());
            target_edit_->setText(source);
        });
        connect(compare_button_, &QPushButton::clicked, this, [this]() { run_compare(); });
        connect(previous_button_, &QPushButton::clicked, this, [this]() { select_previous_difference(); });
        connect(next_button_, &QPushButton::clicked, this, [this]() { select_next_difference(); });
        connect(results_tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_result_item(); });
        connect(results_tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_result_item(); });
        connect(left_view_, &HexView::status_changed, this, [this](qulonglong caret_offset, qulonglong, qulonglong) {
            sync_views_from(left_view_, right_view_, static_cast<qint64>(caret_offset));
        });
        connect(right_view_, &HexView::status_changed, this, [this](qulonglong caret_offset, qulonglong, qulonglong) {
            sync_views_from(right_view_, left_view_, static_cast<qint64>(caret_offset));
        });

        load_settings();
        update_summary_labels();
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        save_settings();
        QDialog::closeEvent(event);
    }

private:
    static QString format_size(qint64 bytes) {
        const double value = static_cast<double>(bytes);
        if (bytes >= (1LL << 30)) {
            return QStringLiteral("%1 GiB").arg(value / static_cast<double>(1LL << 30), 0, 'f', 2);
        }
        if (bytes >= (1LL << 20)) {
            return QStringLiteral("%1 MiB").arg(value / static_cast<double>(1LL << 20), 0, 'f', 2);
        }
        if (bytes >= (1LL << 10)) {
            return QStringLiteral("%1 KiB").arg(value / static_cast<double>(1LL << 10), 0, 'f', 2);
        }
        return QStringLiteral("%1 B").arg(bytes);
    }

    void load_settings() {
        QSettings settings;
        const QString source = settings.value(QStringLiteral("compare/lastSource")).toString();
        const QString target = settings.value(QStringLiteral("compare/lastTarget")).toString();
        if (QFileInfo::exists(source)) {
            source_edit_->setText(source);
        }
        if (QFileInfo::exists(target)) {
            target_edit_->setText(target);
        }
        const QByteArray geometry = settings.value(QStringLiteral("compare/windowGeometry")).toByteArray();
        if (!geometry.isEmpty()) {
            restoreGeometry(geometry);
        }
        const QList<int> vertical_sizes = settings.value(QStringLiteral("compare/verticalSplitter")).value<QList<int>>();
        if (!vertical_sizes.isEmpty() && vertical_splitter_ != nullptr) {
            vertical_splitter_->setSizes(vertical_sizes);
        }
        const QList<int> view_sizes = settings.value(QStringLiteral("compare/viewSplitter")).value<QList<int>>();
        if (!view_sizes.isEmpty() && view_splitter_ != nullptr) {
            view_splitter_->setSizes(view_sizes);
        }
    }

    void save_settings() const {
        QSettings settings;
        settings.setValue(QStringLiteral("compare/lastSource"), source_edit_->text().trimmed());
        settings.setValue(QStringLiteral("compare/lastTarget"), target_edit_->text().trimmed());
        settings.setValue(QStringLiteral("compare/windowGeometry"), saveGeometry());
        if (vertical_splitter_ != nullptr) {
            settings.setValue(QStringLiteral("compare/verticalSplitter"), QVariant::fromValue(vertical_splitter_->sizes()));
        }
        if (view_splitter_ != nullptr) {
            settings.setValue(QStringLiteral("compare/viewSplitter"), QVariant::fromValue(view_splitter_->sizes()));
        }
    }

    void browse_for_file(QLineEdit* edit, const QString& title) {
        const QString start_dir = edit->text().isEmpty() ? QString() : QFileInfo(edit->text()).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, title, start_dir);
        if (!path.isEmpty()) {
            edit->setText(path);
            update_summary_labels();
        }
    }

    void sync_views_from(HexView* source, HexView* target, qint64 offset) {
        if (sync_guard_ || source == nullptr || target == nullptr || !target->has_document()) {
            return;
        }
        sync_guard_ = true;
        target->go_to_offset(offset);
        sync_guard_ = false;
    }

    void update_summary_labels() {
        const QString left_path = source_edit_->text().trimmed();
        const QString right_path = target_edit_->text().trimmed();
        left_label_->setText(left_path.isEmpty() ? QStringLiteral("Source") : QStringLiteral("%1 (Source)").arg(QFileInfo(left_path).fileName()));
        right_label_->setText(right_path.isEmpty() ? QStringLiteral("Target") : QStringLiteral("%1 (Target)").arg(QFileInfo(right_path).fileName()));
        left_label_->setToolTip(left_path);
        right_label_->setToolTip(right_path);

        int selectable_count = 0;
        for (const QVariantMap& row : result_rows_) {
            if (row.value(QStringLiteral("offset")).toLongLong() >= 0) {
                ++selectable_count;
            }
        }
        previous_button_->setEnabled(selectable_count > 0);
        next_button_->setEnabled(selectable_count > 0);

        if (selectable_count == 0) {
            summary_label_->setText(QStringLiteral("Choose two files and run compare."));
            return;
        }

        int current_position = 0;
        if (current_result_index_ >= 0) {
            for (int i = 0; i <= current_result_index_ && i < result_rows_.size(); ++i) {
                if (result_rows_.at(i).value(QStringLiteral("offset")).toLongLong() >= 0) {
                    ++current_position;
                }
            }
        }
        if (current_position > 0) {
            summary_label_->setText(QStringLiteral("Showing %1 difference run(s). %2 of %3 selected.")
                                        .arg(selectable_count)
                                        .arg(current_position)
                                        .arg(selectable_count));
        } else {
            summary_label_->setText(QStringLiteral("Showing %1 difference run(s). Use Previous Difference or Next Difference to navigate.")
                                        .arg(selectable_count));
        }
    }

    void set_status(const QString& text, bool error = false) {
        status_label_->setText(text);
        status_label_->setStyleSheet(error ? QStringLiteral("color: #8b1e1e;") : QString());
    }

    void populate_results_tree(const QString& summary, const QVector<QVariantMap>& rows) {
        results_tree_->clear();
        auto* summary_item = new QTreeWidgetItem(results_tree_, QStringList{QStringLiteral("Summary"), summary, QString(), QString()});
        summary_item->setFlags(summary_item->flags() & ~Qt::ItemIsSelectable);

        for (const QVariantMap& row : rows) {
            const qint64 offset = row.value(QStringLiteral("offset")).toLongLong();
            const qint64 length = row.value(QStringLiteral("length")).toLongLong();
            const QString kind = row.value(QStringLiteral("kind")).toString();
            const QString preview = row.value(QStringLiteral("preview")).toString();
            auto* item = new QTreeWidgetItem(results_tree_, QStringList{
                offset >= 0 ? QStringLiteral("0x%1").arg(offset, 8, 16, QChar(u'0')).toUpper() : QStringLiteral(""),
                kind,
                QString::number(length),
                preview});
            item->setData(0, Qt::UserRole, offset);
            item->setData(1, Qt::UserRole, length);
            if (offset < 0) {
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                item->setForeground(1, QBrush(QColor(120, 120, 120)));
            } else if (kind == QStringLiteral("Changed")) {
                item->setForeground(1, QBrush(QColor(180, 80, 40)));
            } else if (kind == QStringLiteral("Only Left")) {
                item->setForeground(1, QBrush(QColor(180, 40, 40)));
            } else if (kind == QStringLiteral("Only Right")) {
                item->setForeground(1, QBrush(QColor(40, 90, 180)));
            }
        }
    }

    void select_result_index(int index) {
        if (index < 0 || index >= result_rows_.size()) {
            return;
        }
        const QVariantMap row = result_rows_.at(index);
        const qint64 offset = row.value(QStringLiteral("offset")).toLongLong();
        const qint64 length = row.value(QStringLiteral("length")).toLongLong();
        if (offset < 0) {
            return;
        }
        current_result_index_ = index;
        if (length > 0) {
            left_view_->select_range(offset, length);
            right_view_->select_range(offset, length);
        } else {
            left_view_->go_to_offset(offset);
            right_view_->go_to_offset(offset);
        }
        update_summary_labels();
    }

    void activate_result_item() {
        QTreeWidgetItem* item = results_tree_->currentItem();
        if (item == nullptr) {
            return;
        }
        const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
        const qint64 length = item->data(1, Qt::UserRole).toLongLong();
        for (int i = 0; i < result_rows_.size(); ++i) {
            if (result_rows_.at(i).value(QStringLiteral("offset")).toLongLong() == offset &&
                result_rows_.at(i).value(QStringLiteral("length")).toLongLong() == length) {
                select_result_index(i);
                break;
            }
        }
    }

    void select_next_difference() {
        int start = current_result_index_ < 0 ? -1 : current_result_index_;
        for (int i = start + 1; i < result_rows_.size(); ++i) {
            if (result_rows_.at(i).value(QStringLiteral("offset")).toLongLong() >= 0) {
                select_result_index(i);
                return;
            }
        }
        set_status(QStringLiteral("Already at the last difference."));
    }

    void select_previous_difference() {
        int start = current_result_index_ < 0 ? result_rows_.size() : current_result_index_;
        for (int i = start - 1; i >= 0; --i) {
            if (result_rows_.at(i).value(QStringLiteral("offset")).toLongLong() >= 0) {
                select_result_index(i);
                return;
            }
        }
        set_status(QStringLiteral("Already at the first difference."));
    }

    void run_compare() {
        const QString left_path = source_edit_->text().trimmed();
        const QString right_path = target_edit_->text().trimmed();
        if (left_path.isEmpty() || right_path.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Compare Files"), QStringLiteral("Choose both source and target files."));
            return;
        }
        if (!left_view_->open_file(left_path) || !right_view_->open_file(right_path)) {
            QMessageBox::warning(this, QStringLiteral("Compare Files"), QStringLiteral("Failed to open one of the selected files."));
            return;
        }

        save_settings();
        result_rows_.clear();
        current_result_index_ = -1;
        update_summary_labels();

        QProgressDialog progress_dialog(QStringLiteral("Comparing files..."), QStringLiteral("Cancel"), 0, 1000, this);
        progress_dialog.setWindowTitle(QStringLiteral("Compare Files"));
        progress_dialog.setWindowModality(Qt::WindowModal);
        progress_dialog.setMinimumDuration(0);
        progress_dialog.setAutoClose(false);
        progress_dialog.setAutoReset(false);
        progress_dialog.setMinimumWidth(480);
        progress_dialog.setValue(0);
        QPointer<QProgressDialog> dialog_ptr(&progress_dialog);
        std::atomic_bool cancel_requested = false;
        connect(&progress_dialog, &QProgressDialog::canceled, this, [&cancel_requested]() {
            cancel_requested.store(true, std::memory_order_relaxed);
        });

        QFutureWatcher<CompareComputationResult> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcher<CompareComputationResult>::finished, &loop, &QEventLoop::quit);

        watcher.setFuture(QtConcurrent::run([left_path, right_path, dialog_ptr, &cancel_requested]() -> CompareComputationResult {
            CompareComputationResult result;
            QFile left_file(left_path);
            QFile right_file(right_path);
            if (!left_file.open(QIODevice::ReadOnly) || !right_file.open(QIODevice::ReadOnly)) {
                result.error = QStringLiteral("Failed to open one of the selected files.");
                return result;
            }

            const qint64 left_size = left_file.size();
            const qint64 right_size = right_file.size();
            const qint64 shared_size = qMin(left_size, right_size);
            constexpr qint64 kChunkSize = 1 << 20;
            constexpr int kMaxCompareRows = 5000;
            const QColor changed_color(241, 196, 15, 110);
            const QColor left_only_color(231, 76, 60, 90);
            const QColor right_only_color(52, 152, 219, 90);

            qint64 match_runs = 0;
            qint64 match_bytes = 0;
            qint64 omitted_difference_runs = 0;
            qint64 omitted_difference_bytes = 0;
            result.rows.reserve(kMaxCompareRows);

            auto push_row = [&](qint64 offset, qint64 length, const QString& kind, const QByteArray& preview_bytes) {
                if (result.rows.size() >= kMaxCompareRows) {
                    ++omitted_difference_runs;
                    omitted_difference_bytes += length;
                    return;
                }
                QString preview;
                const int shown = qMin(preview_bytes.size(), 16);
                for (int i = 0; i < shown; ++i) {
                    if (i > 0) {
                        preview += QLatin1Char(' ');
                    }
                    preview += QStringLiteral("%1")
                                   .arg(static_cast<quint8>(preview_bytes.at(i)), 2, 16, QChar(u'0'))
                                   .toUpper();
                }
                result.rows.push_back(QVariantMap{
                    {QStringLiteral("offset"), offset},
                    {QStringLiteral("length"), length},
                    {QStringLiteral("kind"), kind},
                    {QStringLiteral("preview"), preview},
                });
            };

            QString current_kind;
            qint64 current_offset = -1;
            qint64 current_length = 0;
            QByteArray current_preview;
            auto flush_run = [&]() {
                if (current_length <= 0 || current_kind.isEmpty()) {
                    return;
                }
                if (current_kind == QStringLiteral("Match")) {
                    ++match_runs;
                    match_bytes += current_length;
                } else {
                    push_row(current_offset, current_length, current_kind, current_preview);
                    if (current_kind == QStringLiteral("Changed")) {
                        result.left_highlights.push_back({current_offset, current_length, changed_color});
                        result.right_highlights.push_back({current_offset, current_length, changed_color});
                    }
                }
                current_kind.clear();
                current_offset = -1;
                current_length = 0;
                current_preview.clear();
            };

            for (qint64 chunk_offset = 0; chunk_offset < shared_size; chunk_offset += kChunkSize) {
                if (cancel_requested.load(std::memory_order_relaxed)) {
                    result.canceled = true;
                    break;
                }
                const qint64 chunk = qMin(kChunkSize, shared_size - chunk_offset);
                const QByteArray left = left_file.read(chunk);
                const QByteArray right = right_file.read(chunk);
                const int compare_len = qMin(left.size(), right.size());
                for (int i = 0; i < compare_len; ++i) {
                    const QString kind = left.at(i) == right.at(i) ? QStringLiteral("Match") : QStringLiteral("Changed");
                    const qint64 absolute_offset = chunk_offset + i;
                    if (kind != current_kind || absolute_offset != current_offset + current_length) {
                        flush_run();
                        current_kind = kind;
                        current_offset = absolute_offset;
                        current_length = 1;
                        current_preview = QByteArray(1, left.at(i));
                    } else {
                        ++current_length;
                        if (current_preview.size() < 16) {
                            current_preview.append(left.at(i));
                        }
                    }
                }

                if (dialog_ptr) {
                    const qint64 completed = chunk_offset + compare_len;
                    QMetaObject::invokeMethod(dialog_ptr.data(), [dialog_ptr, completed, shared_size]() {
                        if (!dialog_ptr) {
                            return;
                        }
                        dialog_ptr->setValue(shared_size > 0 ? static_cast<int>((completed * 1000) / qMax<qint64>(1, shared_size)) : 1000);
                        dialog_ptr->setLabelText(QStringLiteral("Comparing %1 of %2")
                                                     .arg(format_size(completed))
                                                     .arg(format_size(shared_size)));
                    }, Qt::QueuedConnection);
                }
            }
            flush_run();

            if (!result.canceled) {
                if (left_size > shared_size) {
                    left_file.seek(shared_size);
                    const QByteArray preview = left_file.read(qMin<qint64>(16, left_size - shared_size));
                    result.left_highlights.push_back({shared_size, left_size - shared_size, left_only_color});
                    push_row(shared_size, left_size - shared_size, QStringLiteral("Only Left"), preview);
                } else if (right_size > shared_size) {
                    right_file.seek(shared_size);
                    const QByteArray preview = right_file.read(qMin<qint64>(16, right_size - shared_size));
                    result.right_highlights.push_back({shared_size, right_size - shared_size, right_only_color});
                    push_row(shared_size, right_size - shared_size, QStringLiteral("Only Right"), preview);
                }
            }

            if (omitted_difference_runs > 0) {
                result.rows.push_back(QVariantMap{
                    {QStringLiteral("offset"), -1},
                    {QStringLiteral("length"), omitted_difference_bytes},
                    {QStringLiteral("kind"), QStringLiteral("More")},
                    {QStringLiteral("preview"), QStringLiteral("Showing first %1 difference runs").arg(kMaxCompareRows)},
                });
            }

            result.summary = QStringLiteral("%1 difference run(s)%2, %3 match run(s), matched %4, left %5, right %6")
                                 .arg(result.rows.size() - (omitted_difference_runs > 0 ? 1 : 0))
                                 .arg(omitted_difference_runs > 0 ? QStringLiteral(" + %1 more omitted").arg(omitted_difference_runs) : QString())
                                 .arg(match_runs)
                                 .arg(format_size(match_bytes))
                                 .arg(left_size)
                                 .arg(right_size);
            result.ok = !result.canceled;
            return result;
        }));

        progress_dialog.show();
        loop.exec();
        const CompareComputationResult result = watcher.result();
        progress_dialog.setValue(1000);

        if (!result.error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Compare Files"), result.error);
            return;
        }

        left_view_->set_highlight_ranges(result.left_highlights);
        right_view_->set_highlight_ranges(result.right_highlights);
        result_rows_ = result.rows;
        populate_results_tree(result.summary, result.rows);

        for (int i = 0; i < result_rows_.size(); ++i) {
            if (result_rows_.at(i).value(QStringLiteral("offset")).toLongLong() >= 0) {
                current_result_index_ = i;
                break;
            }
        }
        update_summary_labels();
        set_status(result.canceled
                ? QStringLiteral("Compare canceled. Partial results are shown.")
                : QStringLiteral("Compared %1 and %2.").arg(QFileInfo(left_path).fileName(), QFileInfo(right_path).fileName()));
    }

    QLineEdit* source_edit_ = nullptr;
    QLineEdit* target_edit_ = nullptr;
    QPushButton* compare_button_ = nullptr;
    QLabel* left_label_ = nullptr;
    QLabel* right_label_ = nullptr;
    QLabel* summary_label_ = nullptr;
    QPushButton* previous_button_ = nullptr;
    QPushButton* next_button_ = nullptr;
    QSplitter* vertical_splitter_ = nullptr;
    QSplitter* view_splitter_ = nullptr;
    HexView* left_view_ = nullptr;
    HexView* right_view_ = nullptr;
    QTreeWidget* results_tree_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVector<QVariantMap> result_rows_;
    int current_result_index_ = -1;
    bool sync_guard_ = false;
};

class ModelExplorerDialog final : public QDialog {
public:
    explicit ModelExplorerDialog(HexView* hex_view, QWidget* parent = nullptr)
        : QDialog(parent), hex_view_(hex_view) {
        setWindowTitle(QStringLiteral("3D Buffer Explorer"));
        resize(1360, 880);

        auto* root = new QVBoxLayout(this);
        auto* top_bar = new QHBoxLayout();
        file_label_ = new QLabel(QStringLiteral("Current file: none"), this);
        auto* jump_button = new QPushButton(QStringLiteral("Go To Vertex Offset"), this);
        top_bar->addWidget(file_label_, 1);
        top_bar->addWidget(jump_button);
        root->addLayout(top_bar);

        auto* main_splitter = new QSplitter(Qt::Horizontal, this);
        root->addWidget(main_splitter, 1);

        auto* left_panel = new QWidget(main_splitter);
        auto* left_layout = new QVBoxLayout(left_panel);
        left_layout->setContentsMargins(0, 0, 0, 0);

        auto* manual_box = new QGroupBox(QStringLiteral("Manual Interpretation"), left_panel);
        auto* manual_form = new QFormLayout(manual_box);
        layout_combo_ = new QComboBox(manual_box);
        layout_combo_->addItem(QStringLiteral("XYZ together"), static_cast<int>(ModelVertexLayout::XYZTogether));
        layout_combo_->addItem(QStringLiteral("X block, Y block, Z block"), static_cast<int>(ModelVertexLayout::XYZBlocks));
        vertex_offset_edit_ = new QLineEdit(QStringLiteral("0x0"), manual_box);
        vertex_count_spin_ = new QSpinBox(manual_box);
        vertex_count_spin_->setRange(1, 100000000);
        vertex_count_spin_->setValue(512);
        vertex_stride_spin_ = new QSpinBox(manual_box);
        vertex_stride_spin_->setRange(12, 512);
        vertex_stride_spin_->setValue(12);
        x_offset_spin_ = new QSpinBox(manual_box);
        y_offset_spin_ = new QSpinBox(manual_box);
        z_offset_spin_ = new QSpinBox(manual_box);
        for (QSpinBox* spin : {x_offset_spin_, y_offset_spin_, z_offset_spin_}) {
            spin->setRange(0, 508);
        }
        x_offset_spin_->setValue(0);
        y_offset_spin_->setValue(4);
        z_offset_spin_->setValue(8);
        index_offset_edit_ = new QLineEdit(QStringLiteral("0x0"), manual_box);
        index_count_spin_ = new QSpinBox(manual_box);
        index_count_spin_->setRange(0, 100000000);
        index_type_combo_ = new QComboBox(manual_box);
        index_type_combo_->addItem(QStringLiteral("None"), static_cast<int>(ModelIndexType::None));
        index_type_combo_->addItem(QStringLiteral("u16"), static_cast<int>(ModelIndexType::UInt16));
        index_type_combo_->addItem(QStringLiteral("u32"), static_cast<int>(ModelIndexType::UInt32));
        primitive_combo_ = new QComboBox(manual_box);
        primitive_combo_->addItem(QStringLiteral("Triangles"), static_cast<int>(ModelPrimitiveMode::Triangles));
        primitive_combo_->addItem(QStringLiteral("Lines"), static_cast<int>(ModelPrimitiveMode::Lines));
        primitive_combo_->addItem(QStringLiteral("Points"), static_cast<int>(ModelPrimitiveMode::Points));
        auto* preview_button = new QPushButton(QStringLiteral("Preview Layout"), manual_box);
        manual_form->addRow(QStringLiteral("Layout"), layout_combo_);
        manual_form->addRow(QStringLiteral("Vertex offset"), vertex_offset_edit_);
        manual_form->addRow(QStringLiteral("Vertex count"), vertex_count_spin_);
        manual_form->addRow(QStringLiteral("Vertex stride"), vertex_stride_spin_);
        manual_form->addRow(QStringLiteral("Position X offset"), x_offset_spin_);
        manual_form->addRow(QStringLiteral("Position Y offset"), y_offset_spin_);
        manual_form->addRow(QStringLiteral("Position Z offset"), z_offset_spin_);
        manual_form->addRow(QStringLiteral("Index offset"), index_offset_edit_);
        manual_form->addRow(QStringLiteral("Index count"), index_count_spin_);
        manual_form->addRow(QStringLiteral("Index type"), index_type_combo_);
        manual_form->addRow(QStringLiteral("Primitive"), primitive_combo_);
        manual_form->addRow(QString(), preview_button);
        left_layout->addWidget(manual_box);

        auto* scan_box = new QGroupBox(QStringLiteral("Auto Detect"), left_panel);
        auto* scan_form = new QFormLayout(scan_box);
        scan_start_edit_ = new QLineEdit(QStringLiteral("0x0"), scan_box);
        scan_end_edit_ = new QLineEdit(QStringLiteral("0x4000000"), scan_box);
        scan_step_spin_ = new QSpinBox(scan_box);
        scan_step_spin_->setRange(4, 1 << 20);
        scan_step_spin_->setSingleStep(4);
        scan_step_spin_->setValue(256);
        auto* scan_button = new QPushButton(QStringLiteral("Scan For Mesh Candidates"), scan_box);
        scan_form->addRow(QStringLiteral("Scan start"), scan_start_edit_);
        scan_form->addRow(QStringLiteral("Scan end"), scan_end_edit_);
        scan_form->addRow(QStringLiteral("Scan step"), scan_step_spin_);
        scan_form->addRow(QString(), scan_button);
        left_layout->addWidget(scan_box);

        candidates_table_ = new QTableWidget(0, 8, left_panel);
        candidates_table_->setHorizontalHeaderLabels({
            QStringLiteral("Vertex Offset"),
            QStringLiteral("Layout"),
            QStringLiteral("Stride"),
            QStringLiteral("Vertices"),
            QStringLiteral("Score"),
            QStringLiteral("Index Offset"),
            QStringLiteral("Index Type"),
            QStringLiteral("Indices")
        });
        candidates_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        candidates_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        candidates_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        candidates_table_->horizontalHeader()->setStretchLastSection(true);
        left_layout->addWidget(candidates_table_, 1);

        auto* right_panel = new QWidget(main_splitter);
        auto* right_layout = new QVBoxLayout(right_panel);
        right_layout->setContentsMargins(0, 0, 0, 0);
        preview_summary_label_ = new QLabel(QStringLiteral("Preview a manual layout or select a scanned candidate."), right_panel);
        preview_widget_ = new ModelPreviewWidget(right_panel);
        right_layout->addWidget(preview_summary_label_);
        right_layout->addWidget(preview_widget_, 1);

        main_splitter->setStretchFactor(0, 0);
        main_splitter->setStretchFactor(1, 1);
        main_splitter->setSizes({430, 900});

        status_label_ = new QLabel(QStringLiteral("Ready."), this);
        status_label_->setWordWrap(true);
        root->addWidget(status_label_);

        connect(jump_button, &QPushButton::clicked, this, [this]() { jump_to_vertex_offset(); });
        connect(preview_button, &QPushButton::clicked, this, [this]() { preview_current_layout(); });
        connect(scan_button, &QPushButton::clicked, this, [this]() { run_scan(); });
        connect(layout_combo_, &QComboBox::currentIndexChanged, this, [this]() { update_layout_controls(); });
        connect(candidates_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { apply_candidate(row, true); });
        connect(candidates_table_, &QTableWidget::itemSelectionChanged, this, [this]() {
            const int row = candidates_table_->currentRow();
            if (row >= 0) {
                apply_candidate(row, false);
            }
        });

        update_layout_controls();
        load_current_file();
    }

    void refresh_from_current_file() {
        load_current_file(true);
    }

private:
    void set_status(const QString& text, bool error = false) {
        status_label_->setText(text);
        status_label_->setStyleSheet(error ? QStringLiteral("color: #8b1e1e;") : QString());
    }

    void clear_file_specific_state() {
        vertex_offset_edit_->setText(QStringLiteral("0x0"));
        vertex_count_spin_->setValue(512);
        index_offset_edit_->setText(QStringLiteral("0x0"));
        index_count_spin_->setValue(0);
        candidates_.clear();
        candidates_table_->setRowCount(0);
        preview_widget_->set_model(ParsedModelPreview{});
        preview_summary_label_->setText(QStringLiteral("Preview a manual layout or select a scanned candidate."));
    }

    void load_current_file(bool from_refresh = false) {
        if (hex_view_ == nullptr || !hex_view_->has_document() || hex_view_->document_path().isEmpty()) {
            current_file_path_.clear();
            file_label_->setText(QStringLiteral("Current file: none"));
            clear_file_specific_state();
            set_status(QStringLiteral("Open a file in the main editor first."), true);
            return;
        }

        const QString next_path = hex_view_->document_path();
        const bool changed = current_file_path_ != next_path;
        current_file_path_ = next_path;
        const QFileInfo info(current_file_path_);
        file_label_->setText(QStringLiteral("Current file: %1 (%2 bytes)").arg(info.fileName()).arg(info.size()));
        if (changed || from_refresh) {
            clear_file_specific_state();
            scan_start_edit_->setText(QStringLiteral("0x0"));
            scan_end_edit_->setText(format_model_offset(static_cast<quint64>(qMin<qint64>(info.size(), 64LL * 1024 * 1024))));
            set_status(
                changed
                    ? QStringLiteral("Source file changed to %1. Preview and scan results were cleared.").arg(info.fileName())
                    : QStringLiteral("Loaded %1. Adjust a layout or run a heuristic scan.").arg(info.fileName()));
        } else {
            set_status(QStringLiteral("Loaded %1. Adjust a layout or run a heuristic scan.").arg(info.fileName()));
        }
    }

    void jump_to_vertex_offset() {
        if (hex_view_ == nullptr || current_file_path_.isEmpty()) {
            return;
        }
        quint64 offset = 0;
        if (!parse_model_offset(vertex_offset_edit_->text(), &offset)) {
            set_status(QStringLiteral("Enter a valid vertex offset."), true);
            return;
        }
        hex_view_->go_to_offset(static_cast<qint64>(qMin<quint64>(offset, static_cast<quint64>(qMax<qint64>(0, hex_view_->document_size() - 1)))));
    }

    void preview_current_layout() {
        if (current_file_path_.isEmpty()) {
            set_status(QStringLiteral("Open a file in the main editor first."), true);
            return;
        }

        quint64 vertex_offset = 0;
        quint64 index_offset = 0;
        if (!parse_model_offset(vertex_offset_edit_->text(), &vertex_offset) || !parse_model_offset(index_offset_edit_->text(), &index_offset)) {
            set_status(QStringLiteral("Enter valid vertex and index offsets."), true);
            return;
        }

        ParsedModelPreview preview;
        QString error;
        if (!load_model_preview(
                current_file_path_,
                vertex_offset,
                vertex_count_spin_->value(),
                static_cast<ModelVertexLayout>(layout_combo_->currentData().toInt()),
                vertex_stride_spin_->value(),
                x_offset_spin_->value(),
                y_offset_spin_->value(),
                z_offset_spin_->value(),
                index_offset,
                index_count_spin_->value(),
                static_cast<ModelIndexType>(index_type_combo_->currentData().toInt()),
                static_cast<ModelPrimitiveMode>(primitive_combo_->currentData().toInt()),
                &preview,
                &error)) {
            set_status(error, true);
            return;
        }

        preview_widget_->set_model(preview);
        preview_summary_label_->setText(QStringLiteral("%1 vertices, %2 indices, %3")
                                            .arg(preview.vertices.size())
                                            .arg(preview.indices.size())
                                            .arg(model_vertex_layout_label(static_cast<ModelVertexLayout>(layout_combo_->currentData().toInt()))));
        set_status(QStringLiteral("Preview updated."));
    }

    void populate_candidates(const QVector<ModelScanCandidate>& candidates) {
        candidates_ = candidates;
        candidates_table_->setRowCount(candidates.size());
        for (int row = 0; row < candidates.size(); ++row) {
            const ModelScanCandidate& candidate = candidates.at(row);
            auto set_item = [&](int column, const QString& text) {
                auto* item = new QTableWidgetItem(text);
                candidates_table_->setItem(row, column, item);
            };
            set_item(0, format_model_offset(candidate.vertex_offset));
            set_item(1, model_vertex_layout_label(candidate.vertex_layout));
            set_item(2, candidate.vertex_layout == ModelVertexLayout::XYZTogether ? QString::number(candidate.vertex_stride) : QStringLiteral("-"));
            set_item(3, QString::number(candidate.vertex_count));
            set_item(4, QString::number(candidate.score));
            set_item(5, candidate.index_type == ModelIndexType::None ? QStringLiteral("-") : format_model_offset(candidate.index_offset));
            set_item(6, model_index_type_label(candidate.index_type));
            set_item(7, candidate.index_count > 0 ? QString::number(candidate.index_count) : QStringLiteral("-"));
        }
        if (!candidates.isEmpty()) {
            candidates_table_->selectRow(0);
            apply_candidate(0, false);
        }
    }

    void apply_candidate(int row, bool preview_now) {
        if (row < 0 || row >= candidates_.size()) {
            return;
        }
        const ModelScanCandidate& candidate = candidates_.at(row);
        vertex_offset_edit_->setText(format_model_offset(candidate.vertex_offset));
        layout_combo_->setCurrentIndex(layout_combo_->findData(static_cast<int>(candidate.vertex_layout)));
        vertex_stride_spin_->setValue(candidate.vertex_stride);
        vertex_count_spin_->setValue(candidate.vertex_count);
        x_offset_spin_->setValue(0);
        y_offset_spin_->setValue(4);
        z_offset_spin_->setValue(8);
        if (candidate.index_type == ModelIndexType::None) {
            index_offset_edit_->setText(QStringLiteral("0x0"));
            index_count_spin_->setValue(0);
            index_type_combo_->setCurrentIndex(index_type_combo_->findData(static_cast<int>(ModelIndexType::None)));
        } else {
            index_offset_edit_->setText(format_model_offset(candidate.index_offset));
            index_count_spin_->setValue(candidate.index_count);
            index_type_combo_->setCurrentIndex(index_type_combo_->findData(static_cast<int>(candidate.index_type)));
        }
        primitive_combo_->setCurrentIndex(primitive_combo_->findData(static_cast<int>(ModelPrimitiveMode::Triangles)));
        if (preview_now) {
            preview_current_layout();
        }
    }

    void update_layout_controls() {
        const ModelVertexLayout layout = static_cast<ModelVertexLayout>(layout_combo_->currentData().toInt());
        const bool interleaved = layout == ModelVertexLayout::XYZTogether;
        vertex_stride_spin_->setEnabled(interleaved);
        x_offset_spin_->setEnabled(interleaved);
        y_offset_spin_->setEnabled(interleaved);
        z_offset_spin_->setEnabled(interleaved);
        if (!interleaved) {
            set_status(QStringLiteral("Layout uses three float blocks: all X values, then all Y values, then all Z values."), false);
        }
    }

    void run_scan() {
        if (current_file_path_.isEmpty()) {
            set_status(QStringLiteral("Open a file in the main editor first."), true);
            return;
        }

        quint64 start_offset = 0;
        quint64 end_offset = 0;
        if (!parse_model_offset(scan_start_edit_->text(), &start_offset) || !parse_model_offset(scan_end_edit_->text(), &end_offset) || end_offset <= start_offset) {
            set_status(QStringLiteral("Enter a valid scan range."), true);
            return;
        }

        QProgressDialog progress_dialog(QStringLiteral("Scanning for candidate vertex/index buffers..."), QStringLiteral("Cancel"), 0, 1000, this);
        progress_dialog.setWindowTitle(QStringLiteral("3D Buffer Explorer"));
        progress_dialog.setMinimumWidth(460);
        progress_dialog.setWindowModality(Qt::WindowModal);
        progress_dialog.show();

        std::atomic_bool cancel_requested{false};
        connect(&progress_dialog, &QProgressDialog::canceled, this, [&cancel_requested]() {
            cancel_requested.store(true, std::memory_order_relaxed);
        });

        QFutureWatcher<ModelScanResult> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcher<ModelScanResult>::finished, &loop, &QEventLoop::quit);
        QPointer<QProgressDialog> dialog_ptr(&progress_dialog);
        watcher.setFuture(QtConcurrent::run([=, &cancel_requested]() -> ModelScanResult {
            ModelScanResult result;
            result.ok = true;
            result.candidates = scan_model_candidates(
                current_file_path_,
                start_offset,
                end_offset,
                static_cast<quint64>(scan_step_spin_->value()),
                cancel_requested,
                [dialog_ptr](quint64 completed, quint64 total) {
                    if (!dialog_ptr) {
                        return;
                    }
                    QMetaObject::invokeMethod(dialog_ptr.data(), [dialog_ptr, completed, total]() {
                        if (!dialog_ptr) {
                            return;
                        }
                        dialog_ptr->setValue(total > 0 ? static_cast<int>((completed * 1000) / total) : 1000);
                        dialog_ptr->setLabelText(QStringLiteral("Scanning %1 of %2")
                                                     .arg(format_model_offset(completed))
                                                     .arg(format_model_offset(total)));
                    }, Qt::QueuedConnection);
                });
            result.canceled = cancel_requested.load(std::memory_order_relaxed);
            return result;
        }));

        loop.exec();
        progress_dialog.setValue(1000);
        const ModelScanResult result = watcher.result();
        if (!result.ok) {
            set_status(result.error.isEmpty() ? QStringLiteral("Scan failed.") : result.error, true);
            return;
        }
        populate_candidates(result.candidates);
        set_status(result.canceled
                ? QStringLiteral("Scan canceled. Partial candidates are shown.")
                : QStringLiteral("Found %1 candidate layout(s).").arg(result.candidates.size()));
    }

    HexView* hex_view_ = nullptr;
    QString current_file_path_;
    QLabel* file_label_ = nullptr;
    QComboBox* layout_combo_ = nullptr;
    QLineEdit* vertex_offset_edit_ = nullptr;
    QSpinBox* vertex_count_spin_ = nullptr;
    QSpinBox* vertex_stride_spin_ = nullptr;
    QSpinBox* x_offset_spin_ = nullptr;
    QSpinBox* y_offset_spin_ = nullptr;
    QSpinBox* z_offset_spin_ = nullptr;
    QLineEdit* index_offset_edit_ = nullptr;
    QSpinBox* index_count_spin_ = nullptr;
    QComboBox* index_type_combo_ = nullptr;
    QComboBox* primitive_combo_ = nullptr;
    QLineEdit* scan_start_edit_ = nullptr;
    QLineEdit* scan_end_edit_ = nullptr;
    QSpinBox* scan_step_spin_ = nullptr;
    QTableWidget* candidates_table_ = nullptr;
    ModelPreviewWidget* preview_widget_ = nullptr;
    QLabel* preview_summary_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVector<ModelScanCandidate> candidates_;
};

}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Hex Master");
    setup_central_widget();
    setup_docks();
    setup_menu();
    setup_toolbar();
    setup_status_bar();
    restore_session();
}

void MainWindow::setup_menu() {
    auto* fileMenu = menuBar()->addMenu("&File");
    new_action_ = fileMenu->addAction("&New");
    new_action_->setShortcut(QKeySequence::New);
    connect(new_action_, &QAction::triggered, this, &MainWindow::new_file);
    open_action_ = fileMenu->addAction("&Open...");
    open_action_->setShortcut(QKeySequence::Open);
    connect(open_action_, &QAction::triggered, this, &MainWindow::open_file);
    setup_recent_files_menu(fileMenu);
    fileMenu->addSeparator();
    save_action_ = fileMenu->addAction("&Save");
    save_action_->setShortcut(QKeySequence::Save);
    connect(save_action_, &QAction::triggered, this, &MainWindow::save_file);
    save_as_action_ = fileMenu->addAction("Save &As...");
    save_as_action_->setShortcut(QKeySequence::SaveAs);
    connect(save_as_action_, &QAction::triggered, this, &MainWindow::save_file_as);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu("&Edit");
    undo_action_ = editMenu->addAction("&Undo");
    undo_action_->setShortcut(QKeySequence::Undo);
    connect(undo_action_, &QAction::triggered, this, &MainWindow::undo);
    redo_action_ = editMenu->addAction("&Redo");
    redo_action_->setShortcut(QKeySequence::Redo);
    connect(redo_action_, &QAction::triggered, this, &MainWindow::redo);
    editMenu->addSeparator();
    cut_action_ = editMenu->addAction("Cu&t");
    cut_action_->setShortcut(QKeySequence::Cut);
    connect(cut_action_, &QAction::triggered, this, &MainWindow::cut);
    copy_action_ = editMenu->addAction("&Copy");
    copy_action_->setShortcut(QKeySequence::Copy);
    connect(copy_action_, &QAction::triggered, this, &MainWindow::copy);
    copy_as_hex_action_ = editMenu->addAction("Copy as &Hex");
    copy_as_hex_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(copy_as_hex_action_, &QAction::triggered, this, &MainWindow::copy_as_hex);
    paste_action_ = editMenu->addAction("&Paste");
    paste_action_->setShortcut(QKeySequence::Paste);
    connect(paste_action_, &QAction::triggered, this, &MainWindow::paste);
    paste_hex_action_ = editMenu->addAction("Paste H&ex");
    paste_hex_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(paste_hex_action_, &QAction::triggered, this, &MainWindow::paste_hex);
    insert_bytes_action_ = editMenu->addAction("Insert &Bytes...");
    connect(insert_bytes_action_, &QAction::triggered, this, &MainWindow::insert_bytes);
    fill_selection_action_ = editMenu->addAction("&Fill Selection...");
    connect(fill_selection_action_, &QAction::triggered, this, &MainWindow::fill_selection);
    editMenu->addSeparator();
    edit_mode_group_ = new QActionGroup(this);
    edit_mode_group_->setExclusive(true);
    insert_mode_action_ = editMenu->addAction("&Insert Mode");
    insert_mode_action_->setCheckable(true);
    insert_mode_action_->setShortcut(QKeySequence(Qt::Key_Insert));
    overwrite_mode_action_ = editMenu->addAction("&Overwrite Mode");
    overwrite_mode_action_->setCheckable(true);
    overwrite_mode_action_->setChecked(true);
    edit_mode_group_->addAction(insert_mode_action_);
    edit_mode_group_->addAction(overwrite_mode_action_);
    connect(insert_mode_action_, &QAction::triggered, this, &MainWindow::set_insert_mode);
    connect(overwrite_mode_action_, &QAction::triggered, this, &MainWindow::set_overwrite_mode);
    editMenu->addSeparator();
    goto_action_ = editMenu->addAction("&Go To Offset...");
    goto_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(goto_action_, &QAction::triggered, this, &MainWindow::go_to_offset);

    auto* searchMenu = menuBar()->addMenu("&Search");
    find_action_ = searchMenu->addAction("&Find...");
    find_action_->setShortcut(QKeySequence::Find);
    connect(find_action_, &QAction::triggered, this, &MainWindow::find);
    find_next_action_ = searchMenu->addAction("Find &Next");
    find_next_action_->setShortcut(QKeySequence::FindNext);
    connect(find_next_action_, &QAction::triggered, this, &MainWindow::find_next);
    find_previous_action_ = searchMenu->addAction("Find &Previous");
    find_previous_action_->setShortcut(QKeySequence::FindPrevious);
    connect(find_previous_action_, &QAction::triggered, this, &MainWindow::find_previous);
    replace_action_ = searchMenu->addAction("&Replace...");
    replace_action_->setShortcut(QKeySequence::Replace);
    connect(replace_action_, &QAction::triggered, this, &MainWindow::replace);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(inspector_dock_->toggleViewAction());
    viewMenu->addAction(bookmarks_dock_->toggleViewAction());
    viewMenu->addAction(search_results_dock_->toggleViewAction());
    viewMenu->addAction(analysis_dock_->toggleViewAction());
    auto* inspectorEndianMenu = viewMenu->addMenu("Inspector &Endian");
    inspector_endian_group_ = new QActionGroup(this);
    inspector_endian_group_->setExclusive(true);
    auto* little_endian_action = inspectorEndianMenu->addAction("&Little Endian");
    little_endian_action->setCheckable(true);
    little_endian_action->setChecked(true);
    auto* big_endian_action = inspectorEndianMenu->addAction("&Big Endian");
    big_endian_action->setCheckable(true);
    inspector_endian_group_->addAction(little_endian_action);
    inspector_endian_group_->addAction(big_endian_action);
    connect(little_endian_action, &QAction::triggered, this, &MainWindow::set_inspector_little_endian);
    connect(big_endian_action, &QAction::triggered, this, &MainWindow::set_inspector_big_endian);
    setup_view_menu(viewMenu);

    auto* bookmarks_menu = menuBar()->addMenu("&Bookmarks");
    toggle_bookmark_action_ = bookmarks_menu->addAction("&Toggle Bookmark");
    toggle_bookmark_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(toggle_bookmark_action_, &QAction::triggered, this, &MainWindow::toggle_bookmark);
    next_bookmark_action_ = bookmarks_menu->addAction("&Next Bookmark");
    next_bookmark_action_->setShortcut(QKeySequence(Qt::Key_F2));
    connect(next_bookmark_action_, &QAction::triggered, this, &MainWindow::next_bookmark);
    previous_bookmark_action_ = bookmarks_menu->addAction("&Previous Bookmark");
    previous_bookmark_action_->setShortcut(QKeySequence(QStringLiteral("Shift+F2")));
    connect(previous_bookmark_action_, &QAction::triggered, this, &MainWindow::previous_bookmark);
    auto* tools_menu = menuBar()->addMenu("&Tools");
    schema_tool_action_ = tools_menu->addAction("&Schema Editor...");
    connect(schema_tool_action_, &QAction::triggered, this, &MainWindow::open_schema_tool);
    compare_tool_action_ = tools_menu->addAction("&Compare Files...");
    connect(compare_tool_action_, &QAction::triggered, this, &MainWindow::open_compare_tool);
    model_explorer_tool_action_ = tools_menu->addAction("&3D Buffer Explorer...");
    connect(model_explorer_tool_action_, &QAction::triggered, this, &MainWindow::open_model_explorer_tool);
    compute_hashes_action_ = tools_menu->addAction("Co&mpute Hashes");
    connect(compute_hashes_action_, &QAction::triggered, this, &MainWindow::compute_hashes);
    settings_action_ = tools_menu->addAction("&Settings...");
    connect(settings_action_, &QAction::triggered, this, &MainWindow::open_settings);
    auto* help_menu = menuBar()->addMenu("&Help");
    auto* about_action = help_menu->addAction("&About Hex Master");
    connect(about_action, &QAction::triggered, this, &MainWindow::show_about);
}

void MainWindow::setup_recent_files_menu(QMenu* file_menu) {
    recent_files_menu_ = file_menu->addMenu("Open &Recent");
    recent_files_separator_ = file_menu->addSeparator();
    update_recent_files_menu();
}

void MainWindow::update_recent_files_menu() {
    if (recent_files_menu_ == nullptr) {
        return;
    }

    recent_files_menu_->clear();
    const int max_recent = 10;
    int visible_count = 0;
    for (const QString& path : recent_files_) {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            continue;
        }

        auto* action = recent_files_menu_->addAction(QFileInfo(path).fileName());
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, &MainWindow::open_recent_file);
        ++visible_count;
        if (visible_count >= max_recent) {
            break;
        }
    }

    if (visible_count == 0) {
        auto* action = recent_files_menu_->addAction("No Recent Files");
        action->setEnabled(false);
    }

    if (recent_files_separator_ != nullptr) {
        recent_files_separator_->setVisible(visible_count > 0);
    }
}

void MainWindow::restore_session() {
    QSettings settings;
    recent_files_ = settings.value(QStringLiteral("recentFiles")).toStringList();
    goto_offset_history_ = settings.value(QStringLiteral("gotoOffsetHistory")).toStringList();
    search_history_ = settings.value(QStringLiteral("search/history")).toStringList();
    replace_history_ = settings.value(QStringLiteral("replace/history")).toStringList();
    while (search_history_.size() > 15) {
        search_history_.removeLast();
    }
    while (replace_history_.size() > 15) {
        replace_history_.removeLast();
    }
    last_search_display_text_ = settings.value(QStringLiteral("search/lastText")).toString();
    last_search_hex_mode_ = settings.value(QStringLiteral("search/lastHexMode"), false).toBool();
    last_search_input_mode_ = static_cast<SearchInputMode>(
        settings.value(QStringLiteral("search/lastInputMode"), static_cast<int>(SearchInputMode::Text)).toInt());
    last_search_text_encoding_ = static_cast<SearchTextEncoding>(
        settings.value(QStringLiteral("search/lastTextEncoding"), static_cast<int>(SearchTextEncoding::Utf8)).toInt());
    last_search_numeric_type_ = static_cast<NumericSearchType>(
        settings.value(QStringLiteral("search/lastNumericType"), static_cast<int>(NumericSearchType::Unsigned32)).toInt());
    last_search_numeric_byte_order_ = static_cast<SearchByteOrder>(
        settings.value(QStringLiteral("search/lastByteOrder"), static_cast<int>(SearchByteOrder::Little)).toInt());
    last_search_execution_ = static_cast<SearchExecution>(
        settings.value(QStringLiteral("search/lastExecution"), static_cast<int>(SearchExecution::FindNext)).toInt());
    last_search_selection_only_ = settings.value(QStringLiteral("search/lastSelectionOnly"), false).toBool();
    last_replace_display_text_ = settings.value(QStringLiteral("replace/lastText")).toString();
    last_replace_input_mode_ = static_cast<SearchInputMode>(
        settings.value(QStringLiteral("replace/lastInputMode"), static_cast<int>(SearchInputMode::Text)).toInt());
    last_replace_text_encoding_ = static_cast<SearchTextEncoding>(
        settings.value(QStringLiteral("replace/lastTextEncoding"), static_cast<int>(SearchTextEncoding::Utf8)).toInt());
    last_replace_numeric_type_ = static_cast<NumericSearchType>(
        settings.value(QStringLiteral("replace/lastNumericType"), static_cast<int>(NumericSearchType::Unsigned32)).toInt());
    last_replace_numeric_byte_order_ = static_cast<SearchByteOrder>(
        settings.value(QStringLiteral("replace/lastByteOrder"), static_cast<int>(SearchByteOrder::Little)).toInt());
    last_replace_execution_ = static_cast<SearchExecution>(
        settings.value(QStringLiteral("replace/lastExecution"), static_cast<int>(SearchExecution::FindNext)).toInt());
    last_replace_selection_only_ = settings.value(QStringLiteral("replace/lastSelectionOnly"), false).toBool();
    update_recent_files_menu();
    refresh_goto_offset_widgets();
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    const int bytes_per_row = settings.value(QStringLiteral("editor/bytesPerRow"), 16).toInt();
    hex_view_->set_bytes_per_row(bytes_per_row);
    hex_view_->set_bookmark_gutter_visible(settings.value(QStringLiteral("view/showBookmarkGutter"), true).toBool());
    hex_view_->set_row_numbers_visible(settings.value(QStringLiteral("view/showRowNumbers"), false).toBool());
    hex_view_->set_offsets_visible(settings.value(QStringLiteral("view/showOffsets"), true).toBool());
    hex_view_->set_row_number_column_width(settings.value(QStringLiteral("view/rowNumberWidth"), -1).toInt());
    hex_view_->set_offset_column_width(settings.value(QStringLiteral("view/offsetWidth"), -1).toInt());
    if (show_bookmark_gutter_action_ != nullptr) {
        show_bookmark_gutter_action_->setChecked(hex_view_->bookmark_gutter_visible());
    }
    if (show_row_numbers_action_ != nullptr) {
        show_row_numbers_action_->setChecked(hex_view_->row_numbers_visible());
    }
    if (show_offsets_action_ != nullptr) {
        show_offsets_action_->setChecked(hex_view_->offsets_visible());
    }

    const auto endian = settings.value(QStringLiteral("inspector/endian"), QStringLiteral("little")).toString();
    if (endian == QStringLiteral("big")) {
        set_inspector_big_endian();
    } else {
        set_inspector_little_endian();
    }

    const bool restore_last_session = settings.value(QStringLiteral("settings/restoreLastSession"), true).toBool();
    const QString last_file = settings.value(QStringLiteral("session/lastFile")).toString();
    if (restore_last_session && !last_file.isEmpty() && QFileInfo::exists(last_file)) {
        open_file_path(last_file);
    }
}

void MainWindow::save_session() const {
    QSettings settings;
    if (schema_tool_dialog_ != nullptr) {
        static_cast<SchemaToolDialog*>(schema_tool_dialog_)->save_to_settings();
    }
    settings.setValue(QStringLiteral("recentFiles"), recent_files_);
    settings.setValue(QStringLiteral("gotoOffsetHistory"), goto_offset_history_);
    settings.setValue(QStringLiteral("search/history"), search_history_);
    settings.setValue(QStringLiteral("replace/history"), replace_history_);
    settings.setValue(QStringLiteral("search/lastText"), last_search_display_text_);
    settings.setValue(QStringLiteral("search/lastHexMode"), last_search_hex_mode_);
    settings.setValue(QStringLiteral("search/lastInputMode"), static_cast<int>(last_search_input_mode_));
    settings.setValue(QStringLiteral("search/lastTextEncoding"), static_cast<int>(last_search_text_encoding_));
    settings.setValue(QStringLiteral("search/lastNumericType"), static_cast<int>(last_search_numeric_type_));
    settings.setValue(QStringLiteral("search/lastByteOrder"), static_cast<int>(last_search_numeric_byte_order_));
    settings.setValue(QStringLiteral("search/lastExecution"), static_cast<int>(last_search_execution_));
    settings.setValue(QStringLiteral("search/lastSelectionOnly"), last_search_selection_only_);
    settings.setValue(QStringLiteral("replace/lastText"), last_replace_display_text_);
    settings.setValue(QStringLiteral("replace/lastInputMode"), static_cast<int>(last_replace_input_mode_));
    settings.setValue(QStringLiteral("replace/lastTextEncoding"), static_cast<int>(last_replace_text_encoding_));
    settings.setValue(QStringLiteral("replace/lastNumericType"), static_cast<int>(last_replace_numeric_type_));
    settings.setValue(QStringLiteral("replace/lastByteOrder"), static_cast<int>(last_replace_numeric_byte_order_));
    settings.setValue(QStringLiteral("replace/lastExecution"), static_cast<int>(last_replace_execution_));
    settings.setValue(QStringLiteral("replace/lastSelectionOnly"), last_replace_selection_only_);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("editor/bytesPerRow"), hex_view_ ? hex_view_->bytes_per_row() : 16);
    settings.setValue(QStringLiteral("view/showBookmarkGutter"), hex_view_ ? hex_view_->bookmark_gutter_visible() : true);
    settings.setValue(QStringLiteral("view/showRowNumbers"), hex_view_ ? hex_view_->row_numbers_visible() : false);
    settings.setValue(QStringLiteral("view/showOffsets"), hex_view_ ? hex_view_->offsets_visible() : true);
    settings.setValue(QStringLiteral("view/rowNumberWidth"), hex_view_ ? hex_view_->row_number_column_width() : -1);
    settings.setValue(QStringLiteral("view/offsetWidth"), hex_view_ ? hex_view_->offset_column_width() : -1);
    settings.setValue(
        QStringLiteral("inspector/endian"),
        hex_view_ && hex_view_->inspector_endian() == HexView::InspectorEndian::Big ? QStringLiteral("big") : QStringLiteral("little"));
    settings.setValue(QStringLiteral("session/lastFile"), hex_view_ && hex_view_->has_document() ? hex_view_->document_path() : QString());
}

void MainWindow::add_recent_file(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    recent_files_.removeAll(path);
    recent_files_.prepend(path);
    while (recent_files_.size() > 10) {
        recent_files_.removeLast();
    }
    update_recent_files_menu();
}

QString MainWindow::preferred_dialog_directory(const QString& fallback_file_name) const {
    QSettings settings;
    const QString last_directory = settings.value(QStringLiteral("session/lastDirectory")).toString();
    if (!last_directory.isEmpty()) {
        if (fallback_file_name.isEmpty()) {
            return last_directory;
        }
        return QDir(last_directory).filePath(fallback_file_name);
    }

    if (hex_view_ != nullptr && hex_view_->has_document()) {
        const QFileInfo current_info(hex_view_->document_path());
        if (current_info.exists()) {
            if (fallback_file_name.isEmpty()) {
                return current_info.absolutePath();
            }
            return current_info.absoluteDir().filePath(fallback_file_name);
        }
    }

    return fallback_file_name;
}

void MainWindow::remember_dialog_path(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (directory.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("session/lastDirectory"), directory);
}

void MainWindow::add_goto_offset_history(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    goto_offset_history_.removeAll(normalized);
    goto_offset_history_.prepend(normalized);
    while (goto_offset_history_.size() > 10) {
        goto_offset_history_.removeLast();
    }
    refresh_goto_offset_widgets();
}

void MainWindow::add_search_history(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    search_history_.removeAll(normalized);
    search_history_.prepend(normalized);
    while (search_history_.size() > 15) {
        search_history_.removeLast();
    }
}

void MainWindow::add_replace_history(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    replace_history_.removeAll(normalized);
    replace_history_.prepend(normalized);
    while (replace_history_.size() > 15) {
        replace_history_.removeLast();
    }
}

void MainWindow::refresh_goto_offset_widgets() {
    if (goto_offset_edit_ == nullptr) {
        return;
    }

    const QString current_text = goto_offset_edit_->currentText();
    goto_offset_edit_->blockSignals(true);
    goto_offset_edit_->clear();
    goto_offset_edit_->addItems(goto_offset_history_);
    goto_offset_edit_->setCurrentText(current_text);
    goto_offset_edit_->blockSignals(false);
}

MainWindow::SaveBackupPolicy MainWindow::save_backup_policy() const {
    QSettings settings;
    return static_cast<SaveBackupPolicy>(settings.value(QStringLiteral("settings/saveBackupPolicy"), static_cast<int>(SaveBackupPolicy::Ask)).toInt());
}

MainWindow::SaveBackupMode MainWindow::save_backup_mode() const {
    QSettings settings;
    return static_cast<SaveBackupMode>(settings.value(QStringLiteral("settings/saveBackupMode"), static_cast<int>(SaveBackupMode::Progress)).toInt());
}

bool MainWindow::resolve_backup_policy_for_save(const QString& path, SaveBackupPolicy* policy) {
    if (policy == nullptr) {
        return false;
    }

    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists()) {
        *policy = SaveBackupPolicy::Never;
        return true;
    }

    SaveBackupPolicy resolved = save_backup_policy();
    if (resolved == SaveBackupPolicy::Ask) {
        const auto response = QMessageBox::question(
            this,
            QStringLiteral("Create Backup"),
            QStringLiteral("Create a backup copy before saving?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (response == QMessageBox::Cancel) {
            return false;
        }
        resolved = response == QMessageBox::Yes ? SaveBackupPolicy::Always : SaveBackupPolicy::Never;
    }

    *policy = resolved;
    return true;
}

bool MainWindow::confirm_explicit_save(const QString& title) const {
    const auto response = QMessageBox::question(
        const_cast<MainWindow*>(this),
        QStringLiteral("Confirm Save"),
        QStringLiteral("Save changes to %1?").arg(title),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    return response == QMessageBox::Yes;
}

bool MainWindow::copy_file_with_progress(
    const QString& source_path,
    const QString& target_path,
    const std::function<bool(qint64, qint64, const QString&)>& progress_callback) {
    QFile source(source_path);
    if (!source.open(QIODevice::ReadOnly)) {
        return false;
    }

    QFile target(target_path);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const qint64 total = source.size();
    qint64 completed = 0;
    constexpr qint64 kChunkSize = 8 * 1024 * 1024;

    if (progress_callback && !progress_callback(0, total, QStringLiteral("Creating backup"))) {
        return false;
    }

    while (!source.atEnd()) {
        const QByteArray chunk = source.read(kChunkSize);
        if (chunk.isEmpty() && source.error() != QFile::NoError) {
            return false;
        }
        if (!chunk.isEmpty() && target.write(chunk) != chunk.size()) {
            return false;
        }

        completed += chunk.size();
        if (progress_callback && !progress_callback(completed, total, QStringLiteral("Creating backup"))) {
            return false;
        }
    }

    return target.flush();
}

QString MainWindow::backup_path_for(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return QString();
    }

    if (info.suffix().isEmpty()) {
        return path + QStringLiteral(".bak");
    }

    return info.path() + QDir::separator() + info.completeBaseName() + QStringLiteral(".") + info.suffix() + QStringLiteral(".bak");
}

bool MainWindow::prepare_backup_for_save(const QString& path, SaveBackupPolicy policy, const std::function<bool(qint64, qint64, const QString&)>& progress_callback) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists()) {
        return true;
    }

    if (policy == SaveBackupPolicy::Never) {
        return true;
    }

    const QString backup_path = backup_path_for(path);
    if (backup_path.isEmpty()) {
        return true;
    }

    if (QFile::exists(backup_path) && !QFile::remove(backup_path)) {
        QMessageBox::warning(this, QStringLiteral("Backup Failed"), QStringLiteral("Could not replace the existing backup file."));
        return false;
    }

    if (save_backup_mode() == SaveBackupMode::Fast) {
        if (!QFile::copy(path, backup_path)) {
            QMessageBox::warning(this, QStringLiteral("Backup Failed"), QStringLiteral("Could not create a backup copy before saving."));
            return false;
        }
    } else {
        if (!copy_file_with_progress(path, backup_path, progress_callback)) {
            QMessageBox::warning(this, QStringLiteral("Backup Failed"), QStringLiteral("Could not create a backup copy before saving."));
            return false;
        }
    }

    return true;
}

bool MainWindow::save_current_document(bool confirm_save, const QString* save_as_path) {
    if (hex_view_ == nullptr || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before saving."), 3000);
        return false;
    }

    const bool save_as = save_as_path != nullptr || hex_view_->is_read_only();
    QString target_path = save_as_path != nullptr ? *save_as_path : QString();
    if (save_as && target_path.isEmpty()) {
        target_path = QFileDialog::getSaveFileName(this, QStringLiteral("Save File As"), preferred_dialog_directory(QFileInfo(hex_view_->document_path()).fileName()));
        if (target_path.isEmpty()) {
            return false;
        }
    }

    const QString title = save_as ? QFileInfo(target_path).fileName() : hex_view_->document_title();
    if (confirm_save && !confirm_explicit_save(title)) {
        return false;
    }

    const QString backup_source_path = save_as ? target_path : hex_view_->document_path();
    SaveBackupPolicy backup_policy = SaveBackupPolicy::Never;
    if (!resolve_backup_policy_for_save(backup_source_path, &backup_policy)) {
        return false;
    }

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Saving"));
    progress_dialog.setLabelText(QStringLiteral("Preparing save..."));
    progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };
    auto format_duration = [](qint64 seconds) -> QString {
        if (seconds < 60) {
            return QStringLiteral("%1s").arg(seconds);
        }
        if (seconds < 3600) {
            return QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60);
        }
        return QStringLiteral("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
    };

    QElapsedTimer timer;
    timer.start();
    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    bool progress_started = false;
    bool canceled = false;
    const bool fast_in_place_save = !save_as && hex_view_ != nullptr && hex_view_->has_document() &&
        !hex_view_->is_read_only() && hex_view_->document_path() == target_path &&
        hex_view_->can_save_in_place();
    const auto progress_callback = [&](qint64 completed, qint64 total, const QString& phase) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        if (!progress_started) {
            progress_started = true;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            if (fast_in_place_save && phase == QStringLiteral("Saving")) {
                progress_dialog.setCancelButton(nullptr);
            }
            progress_dialog.show();
            QApplication::processEvents();
        }
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }

        last_ui_completed = completed;
        ui_timer.restart();
        const int value = total > 0 ? static_cast<int>((completed * 1000) / total) : 0;
        progress_dialog.setValue(qBound(0, value, 1000));

        QString label = QStringLiteral("%1 %2 of %3").arg(phase).arg(format_bytes(completed)).arg(format_bytes(total));
        if (completed > 0 && total > completed) {
            const qint64 elapsed_ms = timer.elapsed();
            const double bytes_per_ms = static_cast<double>(completed) / qMax<qint64>(1, elapsed_ms);
            if (bytes_per_ms > 0.0) {
                const qint64 remaining_seconds = static_cast<qint64>((total - completed) / bytes_per_ms / 1000.0);
                label += QStringLiteral("\nEstimated time left: %1").arg(format_duration(qMax<qint64>(0, remaining_seconds)));
            }
        }
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label.replace(QLatin1Char('\n'), QLatin1Char(' ')));
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress_dialog.wasCanceled()) {
            canceled = true;
            return false;
        }
        return true;
    };

    if (!prepare_backup_for_save(backup_source_path, backup_policy, progress_callback)) {
        if (progress_started) {
            QApplication::restoreOverrideCursor();
        }
        if (canceled) {
            statusBar()->showMessage(QStringLiteral("Save canceled."), 4000);
        }
        return false;
    }
    const bool saved = save_as
        ? hex_view_->save_as_with_progress(target_path, [&](qint64 completed, qint64 total) {
            return progress_callback(completed, total, QStringLiteral("Saving"));
        })
        : (hex_view_->can_save_in_place()
            ? hex_view_->save_in_place_with_progress([&](qint64 completed, qint64 total) {
                return progress_callback(completed, total, QStringLiteral("Saving"));
            })
            : hex_view_->save_with_progress([&](qint64 completed, qint64 total) {
                return progress_callback(completed, total, QStringLiteral("Saving"));
            }));
    if (progress_started) {
        progress_dialog.setValue(1000);
        QApplication::restoreOverrideCursor();
    }
    if (!saved) {
        if (canceled) {
            statusBar()->showMessage(QStringLiteral("Save canceled."), 4000);
            return false;
        }
        statusBar()->showMessage(save_as ? QStringLiteral("Save As failed.") : QStringLiteral("Save failed."), 4000);
        return false;
    }

    if (save_as) {
        add_recent_file(target_path);
        remember_dialog_path(target_path);
        statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(target_path).fileName()), 3000);
    } else {
        remember_dialog_path(hex_view_->document_path());
        statusBar()->showMessage(QStringLiteral("Saved %1").arg(hex_view_->document_title()), 3000);
    }

    return true;
}

bool MainWindow::confirm_discard_changes() {
    if (hex_view_ == nullptr || !hex_view_->has_document() || !hex_view_->is_dirty()) {
        return true;
    }

    const auto response = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved Changes"),
        QStringLiteral("Save changes to %1 before continuing?").arg(hex_view_->document_title()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (response == QMessageBox::Cancel) {
        return false;
    }

    if (response == QMessageBox::Save) {
        return save_current_document(false);
    }

    return true;
}

bool MainWindow::open_file_path(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }

    if (!confirm_discard_changes()) {
        return false;
    }

    if (!hex_view_->open_file(path)) {
        statusBar()->showMessage(QStringLiteral("Failed to open %1").arg(QFileInfo(path).fileName()), 4000);
        recent_files_.removeAll(path);
        update_recent_files_menu();
        return false;
    }

    add_recent_file(path);
    remember_dialog_path(path);
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirm_discard_changes()) {
        event->ignore();
        return;
    }

    if (schema_tool_dialog_ != nullptr) {
        auto* schema_dialog = static_cast<SchemaToolDialog*>(schema_tool_dialog_);
        if (schema_dialog->isVisible() && !schema_dialog->request_close()) {
            event->ignore();
            return;
        }
        static_cast<SchemaToolDialog*>(schema_tool_dialog_)->save_to_settings();
    }
    save_session();
    QMainWindow::closeEvent(event);
}

void MainWindow::setup_view_menu(QMenu* view_menu) {
    view_menu->addSeparator();
    show_bookmark_gutter_action_ = view_menu->addAction(QStringLiteral("Show Bookmark Gutter"));
    show_bookmark_gutter_action_->setCheckable(true);
    show_bookmark_gutter_action_->setChecked(true);
    connect(show_bookmark_gutter_action_, &QAction::toggled, this, &MainWindow::set_bookmark_gutter_visible);
    show_row_numbers_action_ = view_menu->addAction(QStringLiteral("Show Row Numbers"));
    show_row_numbers_action_->setCheckable(true);
    show_row_numbers_action_->setChecked(false);
    connect(show_row_numbers_action_, &QAction::toggled, this, &MainWindow::set_row_numbers_visible);
    show_offsets_action_ = view_menu->addAction(QStringLiteral("Show Offsets"));
    show_offsets_action_->setCheckable(true);
    show_offsets_action_->setChecked(true);
    connect(show_offsets_action_, &QAction::toggled, this, &MainWindow::set_offsets_visible);
    view_menu->addSeparator();
    decrease_width_action_ = view_menu->addAction(QStringLiteral("Narrower"));
    decrease_width_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    connect(decrease_width_action_, &QAction::triggered, this, &MainWindow::decrease_bytes_per_row);
    increase_width_action_ = view_menu->addAction(QStringLiteral("Wider"));
    increase_width_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+=")));
    connect(increase_width_action_, &QAction::triggered, this, &MainWindow::increase_bytes_per_row);
    reset_view_action_ = view_menu->addAction(QStringLiteral("Reset View"));
    connect(reset_view_action_, &QAction::triggered, this, &MainWindow::reset_view);
    view_menu->addSeparator();
    auto* rowMenu = view_menu->addMenu("Bytes Per &Row");
    row_width_group_ = new QActionGroup(this);
    row_width_group_->setExclusive(true);

    const QList<int> widths = {8, 16, 24, 32, 48};
    for (const int width : widths) {
        auto* action = rowMenu->addAction(QStringLiteral("%1").arg(width));
        action->setCheckable(true);
        action->setData(width);
        if (width == hex_view_->bytes_per_row()) {
            action->setChecked(true);
        }

        row_width_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, width]() {
            set_bytes_per_row(width);
        });
    }
}

void MainWindow::setup_toolbar() {
    toolbar_ = new QToolBar("Main", this);
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar_->setIconSize(QSize(16, 16));
    addToolBar(Qt::TopToolBarArea, toolbar_);

    toolbar_->addAction(open_action_);
    toolbar_->addAction(save_action_);
    toolbar_->addSeparator();
    toolbar_->addAction(undo_action_);
    toolbar_->addAction(redo_action_);
    toolbar_->addSeparator();
    auto* edit_mode_control = new EditModeSegmentedControl(toolbar_);
    edit_mode_control->setFont(toolbar_->font());
    edit_mode_control->set_actions(insert_mode_action_, overwrite_mode_action_);
    edit_mode_toggle_ = edit_mode_control;
    toolbar_->addWidget(edit_mode_toggle_);
    toolbar_->addSeparator();
    toolbar_->addAction(toggle_bookmark_action_);
    toolbar_->addSeparator();
    toolbar_->addAction(find_action_);
    toolbar_->addSeparator();
    auto* goto_label = new QLabel("Goto:", toolbar_);
    goto_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    toolbar_->addWidget(goto_label);
    goto_offset_edit_ = new QComboBox(toolbar_);
    goto_offset_edit_->setEditable(true);
    goto_offset_edit_->setInsertPolicy(QComboBox::NoInsert);
    goto_offset_edit_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    goto_offset_edit_->setMinimumContentsLength(14);
    goto_offset_edit_->setMinimumHeight(28);
    goto_offset_edit_->setMaximumWidth(190);
    goto_offset_edit_->setStyleSheet(QStringLiteral(
        "QComboBox {"
        " min-height: 28px;"
        " padding: 2px 8px;"
        " border: 1px solid #b8b2a6;"
        " border-radius: 7px;"
        " background: #fffdf8;"
        "}"
        "QComboBox::drop-down {"
        " border: 0px;"
        " width: 22px;"
        "}"
        "QComboBox QAbstractItemView {"
        " selection-background-color: #e0d6c2;"
        "}"
    ));
    if (goto_offset_edit_->lineEdit() != nullptr) {
        goto_offset_edit_->lineEdit()->setClearButtonEnabled(true);
        goto_offset_edit_->lineEdit()->setPlaceholderText("0x0");
        goto_offset_edit_->lineEdit()->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    }
    toolbar_->addWidget(goto_offset_edit_);
    auto run_goto = [this]() {
        if (!goto_offset_edit_) {
            return;
        }

        const QString text = goto_offset_edit_->currentText().trimmed();
        quint64 offset = 0;
        if (!try_parse_offset(text, offset)) {
            statusBar()->showMessage(QStringLiteral("Invalid offset."), 2500);
            return;
        }

        if (hex_view_ && hex_view_->has_document()) {
            hex_view_->go_to_offset(static_cast<qint64>(offset));
            add_goto_offset_history(text);
            statusBar()->showMessage(QStringLiteral("Moved to offset 0x%1").arg(offset, 0, 16), 2500);
        }
    };
    if (goto_offset_edit_->lineEdit() != nullptr) {
        connect(goto_offset_edit_->lineEdit(), &QLineEdit::returnPressed, this, run_goto);
    }
    auto* goto_now = toolbar_->addAction("Go");
    connect(goto_now, &QAction::triggered, this, run_goto);
}

void MainWindow::setup_central_widget() {
    hex_view_ = new HexView(this);
    setCentralWidget(hex_view_);

    connect(hex_view_, &HexView::status_changed, this, &MainWindow::update_status);
    connect(hex_view_, &HexView::document_loaded, this, &MainWindow::update_window_title);
    connect(hex_view_, &HexView::bookmarks_changed, this, &MainWindow::update_bookmarks_view);
    connect(hex_view_, &HexView::insert_bytes_requested, this, [this](qulonglong offset) {
        insert_bytes_at(static_cast<qint64>(offset));
    });
    connect(hex_view_, &HexView::document_loaded, this, [this](const QString&, qulonglong) {
        if (model_explorer_dialog_ != nullptr) {
            if (auto* dialog = dynamic_cast<ModelExplorerDialog*>(model_explorer_dialog_)) {
                dialog->refresh_from_current_file();
            }
        }
    });
    connect(hex_view_, &HexView::inspector_changed, this, [this](const QString& text) {
        update_inspector_view(text);
    });
}

void MainWindow::setup_docks() {
    inspector_dock_ = new QDockWidget("Inspector", this);
    inspector_tree_ = new QTreeWidget(inspector_dock_);
    inspector_tree_->setRootIsDecorated(false);
    inspector_tree_->setAlternatingRowColors(true);
    inspector_tree_->setUniformRowHeights(true);
    inspector_tree_->setColumnCount(2);
    inspector_tree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    inspector_tree_->header()->setStretchLastSection(true);
    inspector_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    inspector_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    inspector_tree_->setSelectionBehavior(QAbstractItemView::SelectItems);
    inspector_tree_->setFocusPolicy(Qt::StrongFocus);
    inspector_tree_->setItemDelegate(new TreeValueDelegate(inspector_tree_));
    inspector_tree_->setStyleSheet(QStringLiteral(
        "QTreeWidget::item:selected {"
        " background: #2b6cb0;"
        " color: white;"
        "}"
    ));
    connect(inspector_tree_, &QTreeWidget::itemChanged, this, &MainWindow::handle_inspector_item_changed);
    auto* inspector_copy = new QAction(inspector_tree_);
    inspector_copy->setText(QStringLiteral("Copy Value"));
    inspector_tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    inspector_tree_->addAction(inspector_copy);
    connect(inspector_copy, &QAction::triggered, this, [this]() { copy_current_tree_value(inspector_tree_); });
    inspector_dock_->setWidget(inspector_tree_);
    inspector_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    update_inspector_view(QStringLiteral("Inspector\n\nOpen a file to inspect values."));

    bookmarks_dock_ = new QDockWidget("Bookmarks", this);
    bookmarks_tree_ = new QTreeWidget(bookmarks_dock_);
    bookmarks_tree_->setRootIsDecorated(false);
    bookmarks_tree_->setAlternatingRowColors(true);
    bookmarks_tree_->setUniformRowHeights(true);
    bookmarks_tree_->setColumnCount(4);
    bookmarks_tree_->setHeaderLabels({QStringLiteral("Color"), QStringLiteral("Offset"), QStringLiteral("Row"), QStringLiteral("Label")});
    bookmarks_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    bookmarks_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    bookmarks_tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    bookmarks_tree_->header()->setStretchLastSection(true);
    connect(bookmarks_tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_bookmark_item(); });
    connect(bookmarks_tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_bookmark_item(); });
    auto* rename_bookmark = new QAction(QStringLiteral("Rename Bookmark"), bookmarks_tree_);
    auto* recolor_bookmark = new QAction(QStringLiteral("Change Color"), bookmarks_tree_);
    auto* remove_bookmark = new QAction(QStringLiteral("Remove Bookmark"), bookmarks_tree_);
    bookmarks_tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    bookmarks_tree_->addAction(rename_bookmark);
    bookmarks_tree_->addAction(recolor_bookmark);
    bookmarks_tree_->addAction(remove_bookmark);
    connect(rename_bookmark, &QAction::triggered, this, &MainWindow::rename_current_bookmark);
    connect(recolor_bookmark, &QAction::triggered, this, &MainWindow::recolor_current_bookmark);
    connect(remove_bookmark, &QAction::triggered, this, &MainWindow::remove_current_bookmark);
    bookmarks_dock_->setWidget(bookmarks_tree_);
    bookmarks_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, bookmarks_dock_);
    bookmarks_dock_->hide();
    update_bookmarks_view();

    search_results_dock_ = new QDockWidget("Search Results", this);
    search_results_tabs_ = new QTabWidget(search_results_dock_);
    search_results_tabs_->setTabsClosable(true);
    search_results_tabs_->setDocumentMode(true);
    connect(search_results_tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::close_search_results_tab);
    search_results_dock_->setWidget(search_results_tabs_);
    search_results_dock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, search_results_dock_);
    show_search_summary(QStringLiteral("No active search."));

    analysis_dock_ = new QDockWidget("Analysis", this);
    analysis_tree_ = new QTreeWidget(analysis_dock_);
    analysis_tree_->setRootIsDecorated(false);
    analysis_tree_->setAlternatingRowColors(true);
    analysis_tree_->setUniformRowHeights(true);
    analysis_tree_->setColumnCount(2);
    analysis_tree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    analysis_tree_->header()->setStretchLastSection(true);
    analysis_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    analysis_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    analysis_tree_->setSelectionBehavior(QAbstractItemView::SelectItems);
    analysis_tree_->setFocusPolicy(Qt::StrongFocus);
    analysis_tree_->setItemDelegate(new TreeValueDelegate(analysis_tree_));
    analysis_tree_->setStyleSheet(QStringLiteral(
        "QTreeWidget::item:selected {"
        " background: #2b6cb0;"
        " color: white;"
        "}"
    ));
    auto* analysis_copy = new QAction(analysis_tree_);
    analysis_copy->setText(QStringLiteral("Copy Value"));
    analysis_tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    analysis_tree_->addAction(analysis_copy);
    connect(analysis_copy, &QAction::triggered, this, [this]() { copy_current_tree_value(analysis_tree_); });
    analysis_dock_->setWidget(analysis_tree_);
    analysis_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, analysis_dock_);
    tabifyDockWidget(inspector_dock_, analysis_dock_);
    update_analysis_view(false);

}

void MainWindow::setup_status_bar() {
    status_label_ = new QLabel("Offset: 0x0 | Selection: 0 bytes | Size: 0 bytes | Mode: Overwrite", this);
    statusBar()->addPermanentWidget(status_label_, 1);
}

void MainWindow::new_file() {
    if (!confirm_discard_changes()) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, "New Buffer", preferred_dialog_directory(QStringLiteral("untitled.bin")));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        statusBar()->showMessage(QStringLiteral("Failed to create %1").arg(QFileInfo(path).fileName()), 4000);
        return;
    }
    file.close();

    if (hex_view_->open_file(path)) {
        update_window_title(hex_view_->document_title(), static_cast<qulonglong>(hex_view_->document_size()));
        add_recent_file(path);
        remember_dialog_path(path);
        statusBar()->showMessage(QStringLiteral("Created %1").arg(QFileInfo(path).fileName()), 3000);
    }
}

void MainWindow::open_file() {
    const QString path = QFileDialog::getOpenFileName(this, "Open File", preferred_dialog_directory());
    if (path.isEmpty()) {
        return;
    }

    open_file_path(path);
}

void MainWindow::open_recent_file() {
    auto* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    open_file_path(action->data().toString());
}

void MainWindow::save_file() {
    save_current_document(true);
}

void MainWindow::save_file_as() {
    const QString requested_path;
    save_current_document(true, &requested_path);
}

void MainWindow::undo() {
    if (!hex_view_->has_document()) {
        return;
    }

    if (hex_view_->undo()) {
        statusBar()->showMessage(QStringLiteral("Undo"), 1500);
    }
}

void MainWindow::redo() {
    if (!hex_view_->has_document()) {
        return;
    }

    if (hex_view_->redo()) {
        statusBar()->showMessage(QStringLiteral("Redo"), 1500);
    }
}

void MainWindow::cut() {
    if (!hex_view_ || !hex_view_->has_document() || !hex_view_->has_selection()) {
        statusBar()->showMessage(QStringLiteral("Select a range before cutting."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    copy();
    if (hex_view_->delete_selection()) {
        statusBar()->showMessage(QStringLiteral("Cut selection."), 3000);
    }
}

void MainWindow::copy() {
    QWidget* focus = QApplication::focusWidget();
    if (analysis_tree_ != nullptr && (focus == analysis_tree_ || focus == analysis_tree_->viewport() || analysis_tree_->isAncestorOf(focus))) {
        copy_current_tree_value(analysis_tree_);
        return;
    }

    if (inspector_tree_ != nullptr && (focus == inspector_tree_ || focus == inspector_tree_->viewport() || inspector_tree_->isAncestorOf(focus))) {
        copy_current_tree_value(inspector_tree_);
        return;
    }

    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Nothing to copy."), 2000);
        return;
    }

    const QString text = hex_view_->active_pane() == HexView::ActivePane::Text
        ? hex_view_->selected_text_text()
        : hex_view_->selected_hex_text();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select a range before copying."), 2500);
        return;
    }

    auto* clipboard = QApplication::clipboard();
    clipboard->setText(text);
    statusBar()->showMessage(
        hex_view_->active_pane() == HexView::ActivePane::Text
            ? QStringLiteral("Copied selected text.")
            : QStringLiteral("Copied selected bytes as hex."),
        2500);
}

void MainWindow::copy_as_hex() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Nothing to copy."), 2000);
        return;
    }

    const QString text = hex_view_->selected_hex_text();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select a range before copying."), 2500);
        return;
    }

    QApplication::clipboard()->setText(text);
    statusBar()->showMessage(QStringLiteral("Copied selected bytes as hex."), 2500);
}

void MainWindow::paste() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before pasting."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Clipboard is empty."), 2500);
        return;
    }

    QByteArray bytes;
    if (!try_parse_hex_bytes(text, bytes)) {
        bytes = text.toUtf8();
    }

    const bool ok = hex_view_->edit_mode() == HexView::EditMode::Insert
        ? hex_view_->insert_at_caret(bytes)
        : (hex_view_->has_selection() ? hex_view_->overwrite_selection(bytes) : hex_view_->overwrite_at_caret(bytes));
    if (!ok) {
        statusBar()->showMessage(
            hex_view_->edit_mode() == HexView::EditMode::Insert
                ? QStringLiteral("Paste failed.")
                : QStringLiteral("Paste failed. Overwrite mode requires the byte count to fit the target range."),
            4000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Pasted %1 bytes.").arg(bytes.size()), 2500);
}

void MainWindow::paste_hex() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before pasting."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    const QString text = QApplication::clipboard()->text();
    QByteArray bytes;
    if (!try_parse_hex_bytes(text, bytes)) {
        statusBar()->showMessage(QStringLiteral("Clipboard does not contain valid hex bytes."), 3000);
        return;
    }

    const bool ok = hex_view_->edit_mode() == HexView::EditMode::Insert
        ? hex_view_->insert_at_caret(bytes)
        : (hex_view_->has_selection() ? hex_view_->overwrite_selection(bytes) : hex_view_->overwrite_at_caret(bytes));
    if (!ok) {
        statusBar()->showMessage(
            hex_view_->edit_mode() == HexView::EditMode::Insert
                ? QStringLiteral("Paste Hex failed.")
                : QStringLiteral("Paste Hex failed. Overwrite mode requires the byte count to fit the target range."),
            4000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Pasted %1 hex bytes.").arg(bytes.size()), 2500);
}

void MainWindow::fill_selection() {
    if (!hex_view_ || !hex_view_->has_document() || !hex_view_->has_selection()) {
        statusBar()->showMessage(QStringLiteral("Select a range before filling."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    bool accepted = false;
    const QString text = QInputDialog::getText(
        this,
        QStringLiteral("Fill Selection"),
        QStringLiteral("Byte value (hex, e.g. FF or 0xFF):"),
        QLineEdit::Normal,
        QStringLiteral("00"),
        &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }

    QByteArray bytes;
    if (!try_parse_hex_bytes(text, bytes) || bytes.size() != 1) {
        statusBar()->showMessage(QStringLiteral("Enter exactly one byte in hex."), 3000);
        return;
    }

    const qint64 fill_size = hex_view_->selected_bytes().size();
    if (!hex_view_->fill_selection(static_cast<quint8>(bytes.at(0)))) {
        statusBar()->showMessage(QStringLiteral("Fill failed."), 3000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Filled %1 bytes.").arg(fill_size), 2500);
}

void MainWindow::insert_bytes() {
    insert_bytes_at(hex_view_ ? hex_view_->caret_offset() : 0);
}

void MainWindow::insert_bytes_at(qint64 default_offset) {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before inserting bytes."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Insert Bytes"));
    auto* layout = new QFormLayout(&dialog);

    auto* bytes_edit = new QLineEdit(&dialog);
    bytes_edit->setPlaceholderText(QStringLiteral("DE AD BE EF"));
    auto* append_to_end = new QCheckBox(QStringLiteral("Insert at end of file"), &dialog);
    layout->addRow(QStringLiteral("Bytes (hex)"), bytes_edit);
    layout->addRow(QString(), append_to_end);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QByteArray bytes;
    if (!try_parse_hex_bytes(bytes_edit->text(), bytes) || bytes.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Enter one or more bytes in hex."), 3000);
        return;
    }

    const qint64 insert_offset = append_to_end->isChecked()
        ? hex_view_->document_size()
        : qBound<qint64>(0, default_offset, hex_view_->document_size());
    if (!hex_view_->insert_at_offset(insert_offset, bytes)) {
        statusBar()->showMessage(QStringLiteral("Insert Bytes failed."), 3000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Inserted %1 bytes.").arg(bytes.size()), 2500);
}

void MainWindow::compute_hashes() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before computing hashes."), 2500);
        return;
    }

    const bool selection_only =
        hex_view_->has_selection() &&
        QMessageBox::question(
            this,
            QStringLiteral("Compute Hashes"),
            QStringLiteral("Compute hashes for the current selection only?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes) == QMessageBox::Yes;

    statusBar()->showMessage(QStringLiteral("Computing hashes..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    update_analysis_view(selection_only);
    QApplication::restoreOverrideCursor();
    if (analysis_dock_ != nullptr) {
        analysis_dock_->raise();
        analysis_dock_->show();
    }
    statusBar()->showMessage(QStringLiteral("Computed hashes%1.")
                                 .arg(selection_only ? QStringLiteral(" for the selection") : QStringLiteral(" for the document")),
        3000);
}

void MainWindow::open_schema_tool() {
    if (schema_tool_dialog_ == nullptr) {
        auto* dialog = new SchemaToolDialog(hex_view_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        dialog->load_from_settings();
        schema_tool_dialog_ = dialog;
    }
    schema_tool_dialog_->show();
    schema_tool_dialog_->raise();
    schema_tool_dialog_->activateWindow();
}

void MainWindow::open_compare_tool() {
    if (compare_tool_dialog_ == nullptr) {
        auto* dialog = new CompareToolDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        compare_tool_dialog_ = dialog;
    }
    compare_tool_dialog_->show();
    compare_tool_dialog_->raise();
    compare_tool_dialog_->activateWindow();
}

void MainWindow::open_model_explorer_tool() {
    if (model_explorer_dialog_ == nullptr) {
        auto* dialog = new ModelExplorerDialog(hex_view_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        model_explorer_dialog_ = dialog;
    }
    model_explorer_dialog_->show();
    model_explorer_dialog_->raise();
    model_explorer_dialog_->activateWindow();
}

void MainWindow::open_settings() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Settings"));
    auto* layout = new QFormLayout(&dialog);

    auto* row_width = new QComboBox(&dialog);
    const QList<int> widths = {8, 16, 24, 32, 48};
    for (const int width : widths) {
        row_width->addItem(QString::number(width), width);
    }
    row_width->setCurrentText(QString::number(hex_view_ ? hex_view_->bytes_per_row() : 16));

    QSettings settings;
    auto* restore_session = new QCheckBox(QStringLiteral("Restore last open file on startup"), &dialog);
    restore_session->setChecked(settings.value(QStringLiteral("settings/restoreLastSession"), true).toBool());
    auto* backup_policy = new QComboBox(&dialog);
    backup_policy->addItem(QStringLiteral("Ask"), static_cast<int>(SaveBackupPolicy::Ask));
    backup_policy->addItem(QStringLiteral("Always"), static_cast<int>(SaveBackupPolicy::Always));
    backup_policy->addItem(QStringLiteral("Never"), static_cast<int>(SaveBackupPolicy::Never));
    backup_policy->setCurrentIndex(backup_policy->findData(static_cast<int>(save_backup_policy())));
    auto* backup_mode = new QComboBox(&dialog);
    backup_mode->addItem(QStringLiteral("Progress copy"), static_cast<int>(SaveBackupMode::Progress));
    backup_mode->addItem(QStringLiteral("Fast copy"), static_cast<int>(SaveBackupMode::Fast));
    backup_mode->setCurrentIndex(backup_mode->findData(static_cast<int>(save_backup_mode())));

    layout->addRow(QStringLiteral("Bytes per row"), row_width);
    layout->addRow(QString(), restore_session);
    layout->addRow(QStringLiteral("Backup file"), backup_policy);
    layout->addRow(QStringLiteral("Backup copy mode"), backup_mode);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    set_bytes_per_row(row_width->currentData().toInt());
    settings.setValue(QStringLiteral("settings/restoreLastSession"), restore_session->isChecked());
    settings.setValue(QStringLiteral("settings/saveBackupPolicy"), backup_policy->currentData().toInt());
    settings.setValue(QStringLiteral("settings/saveBackupMode"), backup_mode->currentData().toInt());
    statusBar()->showMessage(QStringLiteral("Settings updated."), 2500);
}

void MainWindow::show_about() {
    QMessageBox::about(
        this,
        QStringLiteral("About Hex Master"),
        QStringLiteral(
            "<b>Hex Master</b><br>"
            "Version %1<br><br>"
            "Created by Majid Siddiqui<br>"
            "me@majidarif.com<br><br>"
            "2026<br><br>"
            "A desktop hex editor built with Qt and Rust.")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::set_insert_mode() {
    if (hex_view_ != nullptr) {
        hex_view_->set_edit_mode(HexView::EditMode::Insert);
    }
}

void MainWindow::set_overwrite_mode() {
    if (hex_view_ != nullptr) {
        hex_view_->set_edit_mode(HexView::EditMode::Overwrite);
    }
}

bool MainWindow::prompt_for_search_pattern(
    const QString& title,
    const QString& label,
    QByteArray& pattern,
    bool& hex_mode,
    SearchExecution* execution,
    bool* selection_only,
    bool default_selection_only,
    QString* display_text) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(520, 0);

    auto* root = new QVBoxLayout(&dialog);
    auto* pattern_group = new QGroupBox(QStringLiteral("Pattern"), &dialog);
    auto* pattern_form = new QFormLayout(pattern_group);
    root->addWidget(pattern_group);

    auto* text_input = new QComboBox(pattern_group);
    text_input->setEditable(true);
    text_input->setInsertPolicy(QComboBox::NoInsert);
    text_input->addItems(search_history_);
    text_input->setCurrentText(last_search_display_text_);
    if (text_input->lineEdit() != nullptr) {
        text_input->lineEdit()->setClearButtonEnabled(true);
    }

    auto* mode_combo = new QComboBox(pattern_group);
    mode_combo->addItem(QStringLiteral("Text"), static_cast<int>(SearchInputMode::Text));
    mode_combo->addItem(QStringLiteral("Hex Bytes"), static_cast<int>(SearchInputMode::Hex));
    mode_combo->addItem(QStringLiteral("Typed Value"), static_cast<int>(SearchInputMode::Value));
    mode_combo->setCurrentIndex(mode_combo->findData(static_cast<int>(last_search_input_mode_)));

    auto* options_stack = new QStackedWidget(pattern_group);

    auto* text_page = new QWidget(options_stack);
    auto* text_form = new QFormLayout(text_page);
    auto* encoding_combo = new QComboBox(text_page);
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Ascii), static_cast<int>(SearchTextEncoding::Ascii));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf8), static_cast<int>(SearchTextEncoding::Utf8));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Le), static_cast<int>(SearchTextEncoding::Utf16Le));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Be), static_cast<int>(SearchTextEncoding::Utf16Be));
    encoding_combo->setCurrentIndex(encoding_combo->findData(static_cast<int>(last_search_text_encoding_)));
    text_form->addRow(QStringLiteral("Text encoding"), encoding_combo);
    options_stack->addWidget(text_page);

    auto* hex_page = new QWidget(options_stack);
    auto* hex_form = new QFormLayout(hex_page);
    auto* hex_hint = new QLabel(QStringLiteral("Search for raw byte sequences like `DE AD BE EF` or `0xDEADBEEF`."), hex_page);
    hex_hint->setWordWrap(true);
    hex_form->addRow(QStringLiteral("Hex mode"), hex_hint);
    options_stack->addWidget(hex_page);

    auto* value_page = new QWidget(options_stack);
    auto* value_form = new QFormLayout(value_page);
    auto* numeric_type_combo = new QComboBox(value_page);
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned8), static_cast<int>(NumericSearchType::Unsigned8));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed8), static_cast<int>(NumericSearchType::Signed8));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned16), static_cast<int>(NumericSearchType::Unsigned16));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed16), static_cast<int>(NumericSearchType::Signed16));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned32), static_cast<int>(NumericSearchType::Unsigned32));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed32), static_cast<int>(NumericSearchType::Signed32));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned64), static_cast<int>(NumericSearchType::Unsigned64));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed64), static_cast<int>(NumericSearchType::Signed64));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Float32), static_cast<int>(NumericSearchType::Float32));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Float64), static_cast<int>(NumericSearchType::Float64));
    numeric_type_combo->setCurrentIndex(numeric_type_combo->findData(static_cast<int>(last_search_numeric_type_)));

    auto* endian_combo = new QComboBox(value_page);
    endian_combo->addItem(QStringLiteral("Little Endian"), static_cast<int>(SearchByteOrder::Little));
    endian_combo->addItem(QStringLiteral("Big Endian"), static_cast<int>(SearchByteOrder::Big));
    endian_combo->setCurrentIndex(endian_combo->findData(static_cast<int>(last_search_numeric_byte_order_)));
    value_form->addRow(QStringLiteral("Value type"), numeric_type_combo);
    value_form->addRow(QStringLiteral("Byte order"), endian_combo);
    options_stack->addWidget(value_page);

    pattern_form->addRow(label, text_input);
    pattern_form->addRow(QStringLiteral("Interpret as"), mode_combo);
    pattern_form->addRow(QStringLiteral("Mode options"), options_stack);

    QComboBox* action_combo = nullptr;
    auto* options_group = new QGroupBox(QStringLiteral("Search Options"), &dialog);
    auto* options_form = new QFormLayout(options_group);
    root->addWidget(options_group);

    if (execution != nullptr) {
        action_combo = new QComboBox(options_group);
        action_combo->addItem(QStringLiteral("Find Next"), static_cast<int>(SearchExecution::FindNext));
        action_combo->addItem(QStringLiteral("Find All"), static_cast<int>(SearchExecution::FindAll));
        action_combo->setCurrentIndex(action_combo->findData(static_cast<int>(last_search_execution_)));
        options_form->addRow(QStringLiteral("Action"), action_combo);
    }

    QCheckBox* selection_only_check = nullptr;
    if (selection_only != nullptr && hex_view_ != nullptr && hex_view_->has_selection()) {
        selection_only_check = new QCheckBox(QStringLiteral("Search in selection only"), options_group);
        selection_only_check->setChecked(default_selection_only || last_search_selection_only_);
        options_form->addRow(QString(), selection_only_check);
    }
    if (action_combo == nullptr && selection_only_check == nullptr) {
        options_group->hide();
    }

    auto update_mode_page = [=]() {
        const SearchInputMode mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
        switch (mode) {
        case SearchInputMode::Text:
            options_stack->setCurrentWidget(text_page);
            break;
        case SearchInputMode::Hex:
            options_stack->setCurrentWidget(hex_page);
            break;
        case SearchInputMode::Value:
            options_stack->setCurrentWidget(value_page);
            break;
        }
    };
    connect(mode_combo, &QComboBox::currentIndexChanged, &dialog, update_mode_page);
    update_mode_page();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString input = text_input->currentText().trimmed();
    if (input.isEmpty()) {
        return false;
    }

    const SearchInputMode selected_mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
    hex_mode = selected_mode != SearchInputMode::Text;
    QByteArray parsed;
    if (selected_mode == SearchInputMode::Hex) {
        last_search_input_mode_ = SearchInputMode::Hex;
        if (!try_parse_hex_bytes(input, parsed)) {
            statusBar()->showMessage(QStringLiteral("Invalid hex byte sequence."), 3000);
            return false;
        }
    } else if (selected_mode == SearchInputMode::Text) {
        last_search_input_mode_ = SearchInputMode::Text;
        last_search_text_encoding_ = static_cast<SearchTextEncoding>(encoding_combo->currentData().toInt());
        parsed = encode_search_text(input, last_search_text_encoding_);
    } else {
        last_search_input_mode_ = SearchInputMode::Value;
        last_search_numeric_type_ = static_cast<NumericSearchType>(numeric_type_combo->currentData().toInt());
        last_search_numeric_byte_order_ = static_cast<SearchByteOrder>(endian_combo->currentData().toInt());
        if (!encode_numeric_search_value(input, last_search_numeric_type_, last_search_numeric_byte_order_, parsed)) {
            statusBar()->showMessage(QStringLiteral("Invalid typed search value."), 3000);
            return false;
        }
    }

    pattern = parsed;
    last_search_display_text_ = input;
    add_search_history(input);
    if (execution != nullptr && action_combo != nullptr) {
        *execution = static_cast<SearchExecution>(action_combo->currentData().toInt());
        last_search_execution_ = *execution;
    }
    if (selection_only != nullptr) {
        *selection_only = selection_only_check != nullptr && selection_only_check->isChecked();
        last_search_selection_only_ = *selection_only;
    }
    if (display_text != nullptr) {
        *display_text = input;
    }
    return true;
}

QByteArray MainWindow::encode_search_text(const QString& text, SearchTextEncoding encoding) {
    switch (encoding) {
    case SearchTextEncoding::Ascii:
        return text.toLatin1();
    case SearchTextEncoding::Utf8:
        return text.toUtf8();
    case SearchTextEncoding::Utf16Le: {
        QByteArray bytes;
        const auto utf16 = text.utf16();
        for (qsizetype index = 0; index < text.size(); ++index) {
            const char16_t value = utf16[index];
            bytes.append(static_cast<char>(value & 0xFF));
            bytes.append(static_cast<char>((value >> 8) & 0xFF));
        }
        return bytes;
    }
    case SearchTextEncoding::Utf16Be: {
        QByteArray bytes;
        const auto utf16 = text.utf16();
        for (qsizetype index = 0; index < text.size(); ++index) {
            const char16_t value = utf16[index];
            bytes.append(static_cast<char>((value >> 8) & 0xFF));
            bytes.append(static_cast<char>(value & 0xFF));
        }
        return bytes;
    }
    }

    return text.toUtf8();
}

QString MainWindow::numeric_type_label(NumericSearchType type) {
    switch (type) {
    case NumericSearchType::Unsigned8:
        return QStringLiteral("u8");
    case NumericSearchType::Signed8:
        return QStringLiteral("i8");
    case NumericSearchType::Unsigned16:
        return QStringLiteral("u16");
    case NumericSearchType::Signed16:
        return QStringLiteral("i16");
    case NumericSearchType::Unsigned32:
        return QStringLiteral("u32");
    case NumericSearchType::Signed32:
        return QStringLiteral("i32");
    case NumericSearchType::Unsigned64:
        return QStringLiteral("u64");
    case NumericSearchType::Signed64:
        return QStringLiteral("i64");
    case NumericSearchType::Float32:
        return QStringLiteral("f32");
    case NumericSearchType::Float64:
        return QStringLiteral("f64");
    }

    return QStringLiteral("u32");
}

bool MainWindow::encode_numeric_search_value(
    const QString& text,
    NumericSearchType type,
    SearchByteOrder byte_order,
    QByteArray& bytes) {
    const auto append_unsigned = [&bytes, byte_order](quint64 value, int width) {
        bytes.clear();
        bytes.reserve(width);
        for (int index = 0; index < width; ++index) {
            const int shift = byte_order == SearchByteOrder::Little ? index * 8 : (width - 1 - index) * 8;
            bytes.append(static_cast<char>((value >> shift) & 0xFFU));
        }
    };

    const auto normalized = text.trimmed();
    bool ok = false;
    switch (type) {
    case NumericSearchType::Unsigned8: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok || value > 0xFFU) {
            return false;
        }
        append_unsigned(value, 1);
        return true;
    }
    case NumericSearchType::Signed8: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok || value < -128 || value > 127) {
            return false;
        }
        append_unsigned(static_cast<quint8>(static_cast<qint8>(value)), 1);
        return true;
    }
    case NumericSearchType::Unsigned16: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok || value > 0xFFFFU) {
            return false;
        }
        append_unsigned(value, 2);
        return true;
    }
    case NumericSearchType::Signed16: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok || value < -32768 || value > 32767) {
            return false;
        }
        append_unsigned(static_cast<quint16>(static_cast<qint16>(value)), 2);
        return true;
    }
    case NumericSearchType::Unsigned32: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok || value > 0xFFFFFFFFULL) {
            return false;
        }
        append_unsigned(value, 4);
        return true;
    }
    case NumericSearchType::Signed32: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok || value < std::numeric_limits<qint32>::min() || value > std::numeric_limits<qint32>::max()) {
            return false;
        }
        append_unsigned(static_cast<quint32>(static_cast<qint32>(value)), 4);
        return true;
    }
    case NumericSearchType::Unsigned64: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok) {
            return false;
        }
        append_unsigned(value, 8);
        return true;
    }
    case NumericSearchType::Signed64: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok) {
            return false;
        }
        append_unsigned(static_cast<quint64>(value), 8);
        return true;
    }
    case NumericSearchType::Float32: {
        const float value = normalized.toFloat(&ok);
        if (!ok) {
            return false;
        }
        bytes.resize(4);
        const auto raw = reinterpret_cast<const unsigned char*>(&value);
        for (int index = 0; index < 4; ++index) {
            const int source_index = byte_order == SearchByteOrder::Little ? index : (3 - index);
            bytes[index] = static_cast<char>(raw[source_index]);
        }
        return true;
    }
    case NumericSearchType::Float64: {
        const double value = normalized.toDouble(&ok);
        if (!ok) {
            return false;
        }
        bytes.resize(8);
        const auto raw = reinterpret_cast<const unsigned char*>(&value);
        for (int index = 0; index < 8; ++index) {
            const int source_index = byte_order == SearchByteOrder::Little ? index : (7 - index);
            bytes[index] = static_cast<char>(raw[source_index]);
        }
        return true;
    }
    }

    return false;
}

QString MainWindow::encoding_label(SearchTextEncoding encoding) {
    switch (encoding) {
    case SearchTextEncoding::Ascii:
        return QStringLiteral("ASCII");
    case SearchTextEncoding::Utf8:
        return QStringLiteral("UTF-8");
    case SearchTextEncoding::Utf16Le:
        return QStringLiteral("UTF-16 LE");
    case SearchTextEncoding::Utf16Be:
        return QStringLiteral("UTF-16 BE");
    }

    return QStringLiteral("UTF-8");
}

void MainWindow::go_to_offset() {
    if (!hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before navigating."), 3000);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Go To Offset"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* prompt = new QLabel(QStringLiteral("Offset"), &dialog);
    prompt->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(prompt);
    auto* combo = new QComboBox(&dialog);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItems(goto_offset_history_);
    combo->setCurrentText(goto_offset_edit_ ? goto_offset_edit_->currentText() : QStringLiteral("0x0"));
    combo->setMinimumWidth(320);
    combo->setMinimumHeight(32);
    if (combo->lineEdit() != nullptr) {
        combo->lineEdit()->setClearButtonEnabled(true);
        combo->lineEdit()->setPlaceholderText(QStringLiteral("0x0"));
    }
    layout->addWidget(combo);

    auto* hint = new QLabel(QStringLiteral("Enter a decimal offset or a hex value prefixed with 0x."), &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #5a6675;"));
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString text = combo->currentText().trimmed();
    if (text.isEmpty()) {
        return;
    }

    quint64 offset = 0;
    if (!try_parse_offset(text, offset)) {
        statusBar()->showMessage(QStringLiteral("Invalid offset: %1").arg(text.trimmed()), 4000);
        return;
    }

    if (goto_offset_edit_ != nullptr) {
        goto_offset_edit_->setCurrentText(text);
    }
    add_goto_offset_history(text);
    hex_view_->go_to_offset(static_cast<qint64>(offset));
    statusBar()->showMessage(QStringLiteral("Moved to offset 0x%1").arg(offset, 0, 16), 3000);
}

void MainWindow::set_bytes_per_row(int bytes_per_row) {
    hex_view_->set_bytes_per_row(bytes_per_row);
    if (row_width_group_ != nullptr) {
        for (QAction* action : row_width_group_->actions()) {
            if (action != nullptr && action->data().toInt() == bytes_per_row) {
                action->setChecked(true);
                break;
            }
        }
    }
    statusBar()->showMessage(QStringLiteral("Bytes per row set to %1").arg(bytes_per_row), 3000);
}

void MainWindow::toggle_bookmark() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before bookmarking."), 2500);
        return;
    }

    hex_view_->toggle_bookmark_at_caret();
    statusBar()->showMessage(QStringLiteral("Toggled bookmark at current offset."), 2000);
}

void MainWindow::next_bookmark() {
    if (!hex_view_ || !hex_view_->has_document()) {
        return;
    }

    hex_view_->next_bookmark();
    statusBar()->showMessage(QStringLiteral("Moved to next bookmark."), 1500);
}

void MainWindow::previous_bookmark() {
    if (!hex_view_ || !hex_view_->has_document()) {
        return;
    }

    hex_view_->previous_bookmark();
    statusBar()->showMessage(QStringLiteral("Moved to previous bookmark."), 1500);
}

void MainWindow::find() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    QByteArray pattern;
    bool hex_mode = false;
    SearchExecution execution = SearchExecution::FindNext;
    bool selection_only = false;
    if (!prompt_for_search_pattern(
            QStringLiteral("Find"),
            QStringLiteral("Pattern"),
            pattern,
            hex_mode,
            &execution,
            &selection_only,
            false)) {
        return;
    }

    last_search_hex_mode_ = hex_mode;
    last_search_pattern_ = pattern;
    if (execution == SearchExecution::FindAll) {
        run_find_all(selection_only);
    } else {
        run_search(true, false, selection_only);
    }
}

void MainWindow::find_in_selection() {
    if (!hex_view_ || !hex_view_->has_document() || !hex_view_->has_selection()) {
        statusBar()->showMessage(QStringLiteral("Select a range before searching in selection."), 2500);
        return;
    }

    QByteArray pattern;
    bool hex_mode = false;
    SearchExecution execution = SearchExecution::FindNext;
    bool selection_only = true;
    if (!prompt_for_search_pattern(
            QStringLiteral("Find"),
            QStringLiteral("Pattern"),
            pattern,
            hex_mode,
            &execution,
            &selection_only,
            true)) {
        return;
    }

    last_search_hex_mode_ = hex_mode;
    last_search_pattern_ = pattern;
    if (execution == SearchExecution::FindAll) {
        run_find_all(selection_only);
    } else {
        run_search(true, false, selection_only);
    }
}

void MainWindow::find_next() {
    run_search(true, true);
}

void MainWindow::find_previous() {
    run_search(false, true);
}

void MainWindow::replace() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before replacing."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    QByteArray before;
    QByteArray after;
    bool hex_mode = false;
    SearchExecution execution = SearchExecution::FindNext;
    bool selection_only = false;
    if (!prompt_for_replace_operation(before, after, hex_mode, execution, selection_only)) {
        return;
    }

    last_search_pattern_ = before;
    last_search_hex_mode_ = hex_mode;

    if (execution == SearchExecution::FindAll) {
        QProgressDialog progress_dialog(this);
        progress_dialog.setWindowTitle(QStringLiteral("Replace All"));
        progress_dialog.setLabelText(QStringLiteral("Replacing..."));
        progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
        progress_dialog.setRange(0, 1000);
        progress_dialog.setValue(0);
        progress_dialog.setMinimumDuration(0);
        progress_dialog.setWindowModality(Qt::ApplicationModal);
        progress_dialog.setMinimumWidth(460);
        progress_dialog.setMaximumWidth(460);

        auto format_bytes = [](qint64 bytes) -> QString {
            static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            double value = static_cast<double>(bytes);
            int unit = 0;
            while (value >= 1024.0 && unit < 4) {
                value /= 1024.0;
                ++unit;
            }
            return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
        };

        QElapsedTimer ui_timer;
        ui_timer.start();
        qint64 last_ui_completed = -1;
        bool canceled = false;
        const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
            constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
            constexpr qint64 kUiUpdateMs = 120;
            const bool force_update = completed <= 0 || completed >= total;
            if (!force_update && last_ui_completed >= 0 &&
                (completed - last_ui_completed) < kUiUpdateBytes &&
                ui_timer.elapsed() < kUiUpdateMs) {
                return true;
            }

            last_ui_completed = completed;
            ui_timer.restart();
            progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
            const QString label = total > 0
                ? QStringLiteral("Replacing... scanned %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
                : QStringLiteral("Replacing...");
            progress_dialog.setLabelText(label);
            statusBar()->showMessage(label);
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            if (progress_dialog.wasCanceled()) {
                canceled = true;
                return false;
            }
            return true;
        };

        QApplication::setOverrideCursor(Qt::WaitCursor);
        progress_dialog.show();
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        bool replace_canceled = false;
        QVector<qint64> replaced_offsets;
        const qint64 replaced = hex_view_->replace_all(before, after, selection_only, &replaced_offsets, &replace_canceled, progress_callback);
        progress_dialog.setValue(1000);
        QApplication::restoreOverrideCursor();
        if (canceled || replace_canceled) {
            if (replaced > 0) {
                show_search_matches(
                    QStringLiteral("Replaced %1 occurrence(s) before canceling%2.")
                        .arg(replaced)
                        .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
                    replaced_offsets,
                    QStringLiteral("Replace"));
                statusBar()->showMessage(QStringLiteral("Replace canceled after %1 occurrence(s).").arg(replaced), 3000);
            } else {
                statusBar()->showMessage(QStringLiteral("Replace canceled."), 2500);
            }
            return;
        }
        if (replaced <= 0) {
            statusBar()->showMessage(QStringLiteral("No matches replaced."), 2500);
            return;
        }

        show_search_matches(
            QStringLiteral("Replaced %1 occurrence(s)%2.")
                .arg(replaced)
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            replaced_offsets,
            QStringLiteral("Replace"));
        statusBar()->showMessage(QStringLiteral("Replaced %1 occurrence(s).").arg(replaced), 3000);
        return;
    }

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Replace"));
    progress_dialog.setLabelText(QStringLiteral("Searching for next match..."));
    progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };
    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    bool canceled = false;
    const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }
        last_ui_completed = completed;
        ui_timer.restart();
        progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
        const QString label = total > 0
            ? QStringLiteral("Searching for next match... %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
            : QStringLiteral("Searching for next match...");
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress_dialog.wasCanceled()) {
            canceled = true;
            return false;
        }
        return true;
    };

    QApplication::setOverrideCursor(Qt::WaitCursor);
    progress_dialog.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    qint64 found_offset = -1;
    bool search_canceled = false;
    const bool found = selection_only
        ? hex_view_->find_pattern_in_selection(before, true, true, &found_offset, &search_canceled, progress_callback)
        : hex_view_->find_pattern(before, true, true, &found_offset, &search_canceled, progress_callback);
    if (!found) {
        const bool fallback_found = selection_only
            ? hex_view_->find_pattern_in_selection(before, true, false, &found_offset, &search_canceled, progress_callback)
            : hex_view_->find_pattern(before, true, false, &found_offset, &search_canceled, progress_callback);
        progress_dialog.setValue(1000);
        QApplication::restoreOverrideCursor();
        if (canceled || search_canceled) {
            statusBar()->showMessage(QStringLiteral("Replace canceled."), 2500);
            return;
        }
        if (!fallback_found) {
            statusBar()->showMessage(QStringLiteral("No match available to replace."), 3000);
            return;
        }
    } else {
        progress_dialog.setValue(1000);
        QApplication::restoreOverrideCursor();
    }

    if (!hex_view_->replace_range(found_offset, before, after)) {
        statusBar()->showMessage(QStringLiteral("Replace failed."), 3000);
        return;
    }

    show_search_matches(
        QStringLiteral("Replaced %1 bytes at 0x%2")
            .arg(after.size())
            .arg(found_offset, 8, 16, QChar(u'0'))
            .toUpper(),
        QVector<qint64>{found_offset},
        QStringLiteral("Replace"));
    statusBar()->showMessage(QStringLiteral("Replaced match at 0x%1").arg(found_offset, 0, 16), 2500);
}

void MainWindow::replace_all() {
    replace();
}

void MainWindow::set_inspector_little_endian() {
    if (hex_view_ != nullptr) {
        hex_view_->set_inspector_endian(HexView::InspectorEndian::Little);
    }
}

void MainWindow::set_inspector_big_endian() {
    if (hex_view_ != nullptr) {
        hex_view_->set_inspector_endian(HexView::InspectorEndian::Big);
    }
}

void MainWindow::set_bookmark_gutter_visible(bool visible) {
    if (hex_view_ != nullptr) {
        hex_view_->set_bookmark_gutter_visible(visible);
    }
}

void MainWindow::set_row_numbers_visible(bool visible) {
    if (hex_view_ != nullptr) {
        hex_view_->set_row_numbers_visible(visible);
    }
}

void MainWindow::set_offsets_visible(bool visible) {
    if (hex_view_ != nullptr) {
        hex_view_->set_offsets_visible(visible);
    }
}

void MainWindow::increase_bytes_per_row() {
    if (hex_view_ == nullptr) {
        return;
    }
    const QList<int> widths = {8, 16, 24, 32, 48};
    const int current = static_cast<int>(hex_view_->bytes_per_row());
    for (const int width : widths) {
        if (width > current) {
            set_bytes_per_row(width);
            return;
        }
    }
}

void MainWindow::decrease_bytes_per_row() {
    if (hex_view_ == nullptr) {
        return;
    }
    const QList<int> widths = {8, 16, 24, 32, 48};
    const int current = static_cast<int>(hex_view_->bytes_per_row());
    for (auto it = widths.crbegin(); it != widths.crend(); ++it) {
        if (*it < current) {
            set_bytes_per_row(*it);
            return;
        }
    }
}

void MainWindow::reset_view() {
    if (hex_view_ == nullptr) {
        return;
    }

    hex_view_->reset_view_layout();
    if (bookmarks_dock_ != nullptr) {
        bookmarks_dock_->hide();
    }
    if (show_bookmark_gutter_action_ != nullptr) {
        show_bookmark_gutter_action_->setChecked(true);
    } else {
        hex_view_->set_bookmark_gutter_visible(true);
    }
    if (show_row_numbers_action_ != nullptr) {
        show_row_numbers_action_->setChecked(false);
    } else {
        hex_view_->set_row_numbers_visible(false);
    }
    if (show_offsets_action_ != nullptr) {
        show_offsets_action_->setChecked(true);
    } else {
        hex_view_->set_offsets_visible(true);
    }
    set_bytes_per_row(16);
    statusBar()->showMessage(QStringLiteral("View reset to defaults."), 2500);
}

void MainWindow::update_status(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size) {
    const bool dirty = hex_view_ && hex_view_->is_dirty();
    const bool read_only = hex_view_ && hex_view_->is_read_only();
    const bool insert_mode = hex_view_ && hex_view_->edit_mode() == HexView::EditMode::Insert;
    status_label_->setText(
        QStringLiteral("Offset: 0x%1 | Selection: %2 bytes | Size: %3 bytes | Row: %4 | Mode: %5")
            .arg(caret_offset, 0, 16)
            .arg(selection_size)
            .arg(document_size)
            .arg(hex_view_ ? hex_view_->bytes_per_row() : 16)
            .arg(read_only
                    ? QStringLiteral("Read Only")
                    : (insert_mode
                            ? (dirty ? QStringLiteral("Insert*") : QStringLiteral("Insert"))
                            : (dirty ? QStringLiteral("Overwrite*") : QStringLiteral("Overwrite")))));
    status_label_->setStyleSheet(dirty
            ? QStringLiteral(
                  "QLabel {"
                  " color: #8b1e1e;"
                  " background: #fde8e8;"
                  " border: 1px solid #f3b2b2;"
                  " border-radius: 4px;"
                  " padding: 2px 6px;"
                  " }")
            : QStringLiteral(
                  "QLabel {"
                  " color: palette(window-text);"
                  " background: transparent;"
                  " border: 0px;"
                  " padding: 0px;"
                  " }"));
    update_save_action_state(dirty);

    if (insert_mode_action_ != nullptr && overwrite_mode_action_ != nullptr) {
        insert_mode_action_->setChecked(insert_mode);
        overwrite_mode_action_->setChecked(!insert_mode);
    }

    if (hex_view_ && hex_view_->has_document() && dirty != last_dirty_state_) {
        last_dirty_state_ = dirty;
        update_window_title(hex_view_->document_title(), document_size);
    }
}

void MainWindow::update_window_title(const QString& title, qulonglong document_size) {
    const bool dirty = hex_view_ && hex_view_->is_dirty();
    last_dirty_state_ = dirty;
    const QString dirty_marker = dirty ? QStringLiteral("*") : QString();
    update_save_action_state(dirty);
    setWindowTitle(QStringLiteral("%1%2 (%3 bytes) - Hex Master").arg(title, dirty_marker).arg(document_size));
}

void MainWindow::update_save_action_state(bool dirty) {
    if (save_action_ == nullptr) {
        return;
    }

    save_action_->setText(QStringLiteral("&Save"));
    save_action_->setToolTip(dirty
            ? QStringLiteral("Save the current document (unsaved changes).")
            : QStringLiteral("Save the current document."));
    save_action_->setStatusTip(dirty
            ? QStringLiteral("Save the current document. Unsaved changes are present.")
            : QStringLiteral("Save the current document."));
}

void MainWindow::update_inspector_view(const QString& text) {
    if (inspector_tree_ == nullptr) {
        return;
    }

    updating_inspector_tree_ = true;
    inspector_tree_->clear();
    if (hex_view_ != nullptr) {
        QString current_section;
        QTreeWidgetItem* current_group = nullptr;
        const QVector<HexView::InspectorRow> rows = hex_view_->inspector_rows();
        for (const HexView::InspectorRow& row : rows) {
            if (row.section != current_section) {
                current_section = row.section;
                current_group = new QTreeWidgetItem(inspector_tree_, QStringList{current_section});
                current_group->setFirstColumnSpanned(true);
                current_group->setExpanded(true);
            }

            if (current_group != nullptr) {
                auto* item = new QTreeWidgetItem(current_group, QStringList{row.field, row.value});
                item->setData(0, Qt::UserRole, row.section);
                item->setData(1, Qt::UserRole, row.field);
                const bool editable =
                    (row.section == QStringLiteral("Overview") && (row.field == QStringLiteral("Bytes") || row.field == QStringLiteral("ASCII"))) ||
                    row.section == QStringLiteral("Integers") ||
                    (row.section == QStringLiteral("Network") && row.field == QStringLiteral("IPv4")) ||
                    row.section == QStringLiteral("Floating Point");
                if (editable) {
                    item->setFlags(item->flags() | Qt::ItemIsEditable);
                }
            }
        }
    }

    if (inspector_tree_->topLevelItemCount() == 0) {
        auto* item = new QTreeWidgetItem(inspector_tree_, QStringList{QStringLiteral("Inspector"), text.trimmed()});
        item->setFirstColumnSpanned(text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("Inspector"));
    }

    inspector_tree_->expandAll();
    updating_inspector_tree_ = false;
}

void MainWindow::update_bookmarks_view() {
    if (bookmarks_tree_ == nullptr) {
        return;
    }

    bookmarks_tree_->clear();
    if (hex_view_ == nullptr || !hex_view_->has_document()) {
        auto* item = new QTreeWidgetItem(bookmarks_tree_, QStringList{QString(), QStringLiteral("Open a file to add bookmarks."), QString(), QString()});
        item->setFirstColumnSpanned(false);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setData(0, Qt::UserRole, -1);
        return;
    }

    const QVector<HexView::BookmarkRow> rows = hex_view_->bookmark_rows();
    if (rows.isEmpty()) {
        auto* item = new QTreeWidgetItem(bookmarks_tree_, QStringList{QString(), QStringLiteral("No bookmarks yet."), QString(), QStringLiteral("Use Ctrl+B at the caret.")});
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setData(0, Qt::UserRole, -1);
        return;
    }

    for (const HexView::BookmarkRow& row : rows) {
        auto* item = new QTreeWidgetItem(bookmarks_tree_, QStringList{
            QStringLiteral("  "),
            QStringLiteral("0x%1").arg(row.offset, 8, 16, QChar(u'0')).toUpper(),
            QString::number(row.row),
            row.label
        });
        item->setData(0, Qt::UserRole, row.offset);
        item->setBackground(0, QBrush(row.color));
        item->setToolTip(0, row.color.name());
        item->setToolTip(1, item->text(1));
    }
}

void MainWindow::update_analysis_view(bool selection_only) {
    if (analysis_tree_ == nullptr) {
        return;
    }

    analysis_tree_->clear();
    if (hex_view_ == nullptr) {
        return;
    }

    QString current_section;
    QTreeWidgetItem* current_group = nullptr;
    const QVector<HexView::AnalysisRow> rows = hex_view_->analysis_rows(selection_only);
    for (const HexView::AnalysisRow& row : rows) {
        if (row.section != current_section) {
            current_section = row.section;
            current_group = new QTreeWidgetItem(analysis_tree_, QStringList{current_section, QString()});
            current_group->setFirstColumnSpanned(true);
            current_group->setFlags(current_group->flags() & ~Qt::ItemIsSelectable);
        }

        if (current_group == nullptr) {
            continue;
        }

        auto* item = new QTreeWidgetItem(current_group, QStringList{row.field, row.value});
        item->setData(0, Qt::UserRole, row.section);
        item->setData(1, Qt::UserRole, row.field);
    }

    analysis_tree_->expandAll();
}

void MainWindow::handle_inspector_item_changed(QTreeWidgetItem* item, int column) {
    if (updating_inspector_tree_ || item == nullptr || column != 1 || hex_view_ == nullptr) {
        return;
    }

    const QString section = item->data(0, Qt::UserRole).toString();
    const QString field = item->data(1, Qt::UserRole).toString();
    if (section.isEmpty() || field.isEmpty()) {
        return;
    }

    QString error_message;
    if (!hex_view_->apply_inspector_edit(section, field, item->text(1), &error_message)) {
        statusBar()->showMessage(error_message.isEmpty() ? QStringLiteral("Inspector edit failed.") : error_message, 4000);
        update_inspector_view(QString());
        return;
    }

    statusBar()->showMessage(QStringLiteral("Updated %1.").arg(field), 2500);
}

void MainWindow::activate_bookmark_item() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    hex_view_->go_to_offset(offset);
    statusBar()->showMessage(QStringLiteral("Moved to bookmark at 0x%1").arg(offset, 0, 16), 2000);
}

void MainWindow::rename_current_bookmark() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    bool ok = false;
    const QString label = QInputDialog::getText(this, QStringLiteral("Rename Bookmark"), QStringLiteral("Label"), QLineEdit::Normal, item->text(3), &ok);
    if (!ok) {
        return;
    }
    if (hex_view_->set_bookmark_label(offset, label)) {
        statusBar()->showMessage(QStringLiteral("Bookmark updated."), 2000);
    }
}

void MainWindow::recolor_current_bookmark() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    const QColor current = item->background(0).color();
    const QColor chosen = QColorDialog::getColor(current, this, QStringLiteral("Choose Bookmark Color"));
    if (!chosen.isValid()) {
        return;
    }
    if (hex_view_->set_bookmark_color(offset, chosen)) {
        statusBar()->showMessage(QStringLiteral("Bookmark color updated."), 2000);
    }
}

void MainWindow::remove_current_bookmark() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    if (hex_view_->remove_bookmark(offset)) {
        statusBar()->showMessage(QStringLiteral("Bookmark removed."), 2000);
    }
}

void MainWindow::copy_current_tree_value(QTreeWidget* tree) {
    if (tree == nullptr) {
        return;
    }

    QTreeWidgetItem* item = tree->currentItem();
    if (item == nullptr && !tree->selectedItems().isEmpty()) {
        item = tree->selectedItems().first();
    }
    if (item == nullptr) {
        return;
    }

    QString value = item->text(1);
    if (value.isEmpty()) {
        const int current_column = tree->currentColumn();
        value = item->text(current_column >= 0 ? current_column : 0);
    }
    if (value.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(value);
    statusBar()->showMessage(QStringLiteral("Copied value to clipboard."), 2000);
}

void MainWindow::close_search_results_tab(int index) {
    if (search_results_tabs_ == nullptr || index < 0 || index >= search_results_tabs_->count()) {
        return;
    }

    QWidget* page = search_results_tabs_->widget(index);
    search_results_tabs_->removeTab(index);
    delete page;
    if (search_results_tabs_->count() == 0) {
        show_search_summary(QStringLiteral("No active search."), QStringLiteral("Summary"));
    }
}

void MainWindow::show_search_summary(const QString& summary, const QString& tab_title) {
    if (search_results_tabs_ == nullptr) {
        return;
    }

    auto* tree = new QTreeWidget(search_results_tabs_);
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setColumnCount(3);
    tree->setHeaderLabels({QStringLiteral("Offset"), QStringLiteral("Match"), QStringLiteral("Preview")});
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });

    auto* item = new QTreeWidgetItem(tree, QStringList{QStringLiteral("Summary"), summary, QString()});
    item->setFirstColumnSpanned(false);

    const QString title = tab_title.isEmpty() ? QStringLiteral("Search %1").arg(++search_results_counter_) : tab_title;
    const int index = search_results_tabs_->addTab(tree, title);
    search_results_tabs_->setCurrentIndex(index);
}

void MainWindow::show_search_matches(const QString& summary, const QVector<qint64>& matches, const QString& tab_title) {
    if (search_results_tabs_ == nullptr) {
        return;
    }

    auto* tree = new QTreeWidget(search_results_tabs_);
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setColumnCount(3);
    tree->setHeaderLabels({QStringLiteral("Offset"), QStringLiteral("Match"), QStringLiteral("Preview")});
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });

    auto* summary_item = new QTreeWidgetItem(tree, QStringList{QStringLiteral("Summary"), summary, QString()});
    summary_item->setFirstColumnSpanned(false);

    auto* results_root = new QTreeWidgetItem(
        tree,
        QStringList{QStringLiteral("Results"), QStringLiteral("%1 match(es)").arg(matches.size()), QString()});
    results_root->setExpanded(true);

    const int max_results = 512;
    const int shown = qMin(matches.size(), max_results);
    for (int index = 0; index < shown; ++index) {
        const qint64 offset = matches.at(index);
        const QByteArray preview_bytes = hex_view_ != nullptr ? hex_view_->read_bytes(offset, qMax(16, last_search_pattern_.size())) : QByteArray();
        QString preview;
        for (int byte_index = 0; byte_index < preview_bytes.size(); ++byte_index) {
            if (byte_index > 0) {
                preview += QLatin1Char(' ');
            }
            preview += QStringLiteral("%1")
                           .arg(static_cast<quint8>(preview_bytes.at(byte_index)), 2, 16, QChar(u'0'))
                           .toUpper();
        }
        auto* item = new QTreeWidgetItem(
            results_root,
            QStringList{
                QStringLiteral("0x%1").arg(offset, 8, 16, QChar(u'0')).toUpper(),
                QStringLiteral("Match %1").arg(index + 1),
                preview});
        item->setData(0, Qt::UserRole, offset);
        item->setData(1, Qt::UserRole, last_search_pattern_.size());
    }

    if (matches.size() > max_results) {
        new QTreeWidgetItem(
            results_root,
            QStringList{
                QStringLiteral("More"),
                QStringLiteral("Showing first %1 matches").arg(max_results),
                QString()});
    }

    tree->expandAll();
    const QString title = tab_title.isEmpty() ? QStringLiteral("Search %1").arg(++search_results_counter_) : tab_title;
    const int index = search_results_tabs_->addTab(tree, title);
    search_results_tabs_->setCurrentIndex(index);
}

void MainWindow::activate_search_result_item() {
    if (search_results_tabs_ == nullptr) {
        return;
    }

    auto* tree = qobject_cast<QTreeWidget*>(search_results_tabs_->currentWidget());
    if (tree == nullptr) {
        return;
    }

    QTreeWidgetItem* item = tree->currentItem();
    if (item == nullptr) {
        return;
    }

    const QVariant offset_value = item->data(0, Qt::UserRole);
    if (!offset_value.isValid()) {
        return;
    }

    const qint64 offset = offset_value.toLongLong();
    const qint64 match_length = item->data(1, Qt::UserRole).toLongLong();

    if (hex_view_ == nullptr || !hex_view_->has_document()) {
        return;
    }

    if (match_length > 0) {
        hex_view_->select_range(offset, match_length);
    } else {
        hex_view_->go_to_offset(offset);
    }
    statusBar()->showMessage(QStringLiteral("Moved to search result at 0x%1").arg(offset, 0, 16), 2500);
}

bool MainWindow::prompt_for_replace_operation(
    QByteArray& before,
    QByteArray& after,
    bool& find_hex_mode,
    SearchExecution& execution,
    bool& selection_only) {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Replace"));
    dialog.resize(560, 0);

    auto* root = new QVBoxLayout(&dialog);

    auto build_mode_section = [&](QWidget* parent,
                                  const QString& title,
                                  const QString& label,
                                  const QString& initial_text,
                                  SearchInputMode initial_mode,
                                  SearchTextEncoding initial_encoding,
                                  NumericSearchType initial_numeric_type,
                                  SearchByteOrder initial_byte_order,
                                  const QStringList& history,
                                  QComboBox*& text_input,
                                  QComboBox*& mode_combo,
                                  QComboBox*& encoding_combo,
                                  QComboBox*& numeric_type_combo,
                                  QComboBox*& byte_order_combo) {
        auto* group = new QGroupBox(title, parent);
        auto* form = new QFormLayout(group);

        text_input = new QComboBox(group);
        text_input->setEditable(true);
        text_input->setInsertPolicy(QComboBox::NoInsert);
        text_input->addItems(history);
        text_input->setCurrentText(initial_text);
        if (text_input->lineEdit() != nullptr) {
            text_input->lineEdit()->setClearButtonEnabled(true);
        }

        mode_combo = new QComboBox(group);
        mode_combo->addItem(QStringLiteral("Text"), static_cast<int>(SearchInputMode::Text));
        mode_combo->addItem(QStringLiteral("Hex Bytes"), static_cast<int>(SearchInputMode::Hex));
        mode_combo->addItem(QStringLiteral("Typed Value"), static_cast<int>(SearchInputMode::Value));
        mode_combo->setCurrentIndex(mode_combo->findData(static_cast<int>(initial_mode)));

        auto* options_stack = new QStackedWidget(group);

        auto* text_page = new QWidget(options_stack);
        auto* text_form = new QFormLayout(text_page);
        encoding_combo = new QComboBox(text_page);
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Ascii), static_cast<int>(SearchTextEncoding::Ascii));
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf8), static_cast<int>(SearchTextEncoding::Utf8));
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Le), static_cast<int>(SearchTextEncoding::Utf16Le));
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Be), static_cast<int>(SearchTextEncoding::Utf16Be));
        encoding_combo->setCurrentIndex(encoding_combo->findData(static_cast<int>(initial_encoding)));
        text_form->addRow(QStringLiteral("Text encoding"), encoding_combo);
        options_stack->addWidget(text_page);

        auto* hex_page = new QWidget(options_stack);
        auto* hex_form = new QFormLayout(hex_page);
        auto* hex_hint = new QLabel(QStringLiteral("Use byte sequences like `4D 5A`, `DE AD BE EF`, or `0xCAFEBABE`."), hex_page);
        hex_hint->setWordWrap(true);
        hex_form->addRow(QStringLiteral("Hex mode"), hex_hint);
        options_stack->addWidget(hex_page);

        auto* value_page = new QWidget(options_stack);
        auto* value_form = new QFormLayout(value_page);
        numeric_type_combo = new QComboBox(value_page);
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned8), static_cast<int>(NumericSearchType::Unsigned8));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed8), static_cast<int>(NumericSearchType::Signed8));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned16), static_cast<int>(NumericSearchType::Unsigned16));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed16), static_cast<int>(NumericSearchType::Signed16));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned32), static_cast<int>(NumericSearchType::Unsigned32));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed32), static_cast<int>(NumericSearchType::Signed32));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned64), static_cast<int>(NumericSearchType::Unsigned64));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed64), static_cast<int>(NumericSearchType::Signed64));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Float32), static_cast<int>(NumericSearchType::Float32));
        numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Float64), static_cast<int>(NumericSearchType::Float64));
        numeric_type_combo->setCurrentIndex(numeric_type_combo->findData(static_cast<int>(initial_numeric_type)));

        byte_order_combo = new QComboBox(value_page);
        byte_order_combo->addItem(QStringLiteral("Little Endian"), static_cast<int>(SearchByteOrder::Little));
        byte_order_combo->addItem(QStringLiteral("Big Endian"), static_cast<int>(SearchByteOrder::Big));
        byte_order_combo->setCurrentIndex(byte_order_combo->findData(static_cast<int>(initial_byte_order)));

        value_form->addRow(QStringLiteral("Value type"), numeric_type_combo);
        value_form->addRow(QStringLiteral("Byte order"), byte_order_combo);
        options_stack->addWidget(value_page);

        form->addRow(label, text_input);
        form->addRow(QStringLiteral("Interpret as"), mode_combo);
        form->addRow(QStringLiteral("Mode options"), options_stack);

        auto update_mode_page = [=]() {
            const SearchInputMode mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
            switch (mode) {
            case SearchInputMode::Text:
                options_stack->setCurrentWidget(text_page);
                break;
            case SearchInputMode::Hex:
                options_stack->setCurrentWidget(hex_page);
                break;
            case SearchInputMode::Value:
                options_stack->setCurrentWidget(value_page);
                break;
            }
        };
        connect(mode_combo, &QComboBox::currentIndexChanged, &dialog, update_mode_page);
        update_mode_page();
        root->addWidget(group);
    };

    QComboBox* find_text_input = nullptr;
    QComboBox* find_mode_combo = nullptr;
    QComboBox* find_encoding_combo = nullptr;
    QComboBox* find_numeric_type_combo = nullptr;
    QComboBox* find_byte_order_combo = nullptr;
    build_mode_section(
        &dialog,
        QStringLiteral("Find"),
        QStringLiteral("Find pattern"),
        last_search_display_text_,
        last_search_input_mode_,
        last_search_text_encoding_,
        last_search_numeric_type_,
        last_search_numeric_byte_order_,
        search_history_,
        find_text_input,
        find_mode_combo,
        find_encoding_combo,
        find_numeric_type_combo,
        find_byte_order_combo);

    QComboBox* replace_text_input = nullptr;
    QComboBox* replace_mode_combo = nullptr;
    QComboBox* replace_encoding_combo = nullptr;
    QComboBox* replace_numeric_type_combo = nullptr;
    QComboBox* replace_byte_order_combo = nullptr;
    build_mode_section(
        &dialog,
        QStringLiteral("Replace With"),
        QStringLiteral("Replace with"),
        last_replace_display_text_,
        last_replace_input_mode_,
        last_replace_text_encoding_,
        last_replace_numeric_type_,
        last_replace_numeric_byte_order_,
        replace_history_,
        replace_text_input,
        replace_mode_combo,
        replace_encoding_combo,
        replace_numeric_type_combo,
        replace_byte_order_combo);

    auto* options_group = new QGroupBox(QStringLiteral("Replace Options"), &dialog);
    auto* options_form = new QFormLayout(options_group);
    auto* action_combo = new QComboBox(options_group);
    action_combo->addItem(QStringLiteral("Replace Next"), static_cast<int>(SearchExecution::FindNext));
    action_combo->addItem(QStringLiteral("Replace All"), static_cast<int>(SearchExecution::FindAll));
    action_combo->setCurrentIndex(action_combo->findData(static_cast<int>(last_replace_execution_)));
    options_form->addRow(QStringLiteral("Action"), action_combo);

    QCheckBox* selection_only_check = nullptr;
    if (hex_view_ != nullptr && hex_view_->has_selection()) {
        selection_only_check = new QCheckBox(QStringLiteral("Search in selection only"), options_group);
        selection_only_check->setChecked(last_replace_selection_only_);
        options_form->addRow(QString(), selection_only_check);
    }
    root->addWidget(options_group);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString find_text = find_text_input->currentText().trimmed();
    const QString replace_text = replace_text_input->currentText().trimmed();
    if (find_text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Enter a find pattern."), 3000);
        return false;
    }

    auto encode_input = [&](const QString& text,
                            QComboBox* mode_combo,
                            QComboBox* encoding_combo,
                            QComboBox* numeric_type_combo,
                            QComboBox* byte_order_combo,
                            QByteArray& bytes,
                            SearchInputMode& saved_mode,
                            SearchTextEncoding& saved_encoding,
                            NumericSearchType& saved_numeric_type,
                            SearchByteOrder& saved_byte_order) -> bool {
        const SearchInputMode mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
        saved_mode = mode;
        switch (mode) {
        case SearchInputMode::Text:
            saved_encoding = static_cast<SearchTextEncoding>(encoding_combo->currentData().toInt());
            bytes = encode_search_text(text, saved_encoding);
            return true;
        case SearchInputMode::Hex:
            return try_parse_hex_bytes(text, bytes);
        case SearchInputMode::Value:
            saved_numeric_type = static_cast<NumericSearchType>(numeric_type_combo->currentData().toInt());
            saved_byte_order = static_cast<SearchByteOrder>(byte_order_combo->currentData().toInt());
            return encode_numeric_search_value(text, saved_numeric_type, saved_byte_order, bytes);
        }
        return false;
    };

    SearchInputMode find_mode = last_search_input_mode_;
    if (!encode_input(
            find_text,
            find_mode_combo,
            find_encoding_combo,
            find_numeric_type_combo,
            find_byte_order_combo,
            before,
            find_mode,
            last_search_text_encoding_,
            last_search_numeric_type_,
            last_search_numeric_byte_order_)) {
        statusBar()->showMessage(QStringLiteral("Invalid find value."), 3000);
        return false;
    }

    SearchInputMode replace_mode = SearchInputMode::Text;
    SearchTextEncoding replace_encoding = last_search_text_encoding_;
    NumericSearchType replace_numeric_type = last_search_numeric_type_;
    SearchByteOrder replace_byte_order = last_search_numeric_byte_order_;
    if (!encode_input(
            replace_text,
            replace_mode_combo,
            replace_encoding_combo,
            replace_numeric_type_combo,
            replace_byte_order_combo,
            after,
            replace_mode,
            replace_encoding,
            replace_numeric_type,
            replace_byte_order)) {
        statusBar()->showMessage(QStringLiteral("Invalid replacement value."), 3000);
        return false;
    }

    if (hex_view_->edit_mode() == HexView::EditMode::Overwrite && replace_mode == SearchInputMode::Hex) {
        if (after.size() > before.size()) {
            QMessageBox::warning(
                this,
                QStringLiteral("Replacement Too Long"),
                QStringLiteral("Raw hex replacement cannot be longer than the source pattern in overwrite mode."));
            return false;
        }
        if (after.size() < before.size()) {
            const auto response = QMessageBox::question(
                this,
                QStringLiteral("Pad Replacement"),
                QStringLiteral("The replacement byte sequence is shorter than the source. Pad the remaining bytes with 00?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                QMessageBox::Yes);
            if (response == QMessageBox::Cancel) {
                return false;
            }
            if (response == QMessageBox::Yes) {
                after.append(QByteArray(before.size() - after.size(), '\0'));
            }
        }
    }

    if (hex_view_->edit_mode() == HexView::EditMode::Overwrite && before.size() != after.size()) {
        statusBar()->showMessage(QStringLiteral("Replacement must match the original length in overwrite mode."), 4000);
        return false;
    }

    find_hex_mode = (find_mode == SearchInputMode::Hex || find_mode == SearchInputMode::Value);
    execution = static_cast<SearchExecution>(action_combo->currentData().toInt());
    selection_only = selection_only_check != nullptr && selection_only_check->isChecked();
    last_search_input_mode_ = find_mode;
    last_search_display_text_ = find_text;
    last_search_execution_ = execution;
    last_search_selection_only_ = selection_only;
    last_replace_display_text_ = replace_text;
    last_replace_input_mode_ = replace_mode;
    last_replace_text_encoding_ = replace_encoding;
    last_replace_numeric_type_ = replace_numeric_type;
    last_replace_numeric_byte_order_ = replace_byte_order;
    last_replace_execution_ = execution;
    last_replace_selection_only_ = selection_only;
    add_search_history(find_text);
    add_replace_history(replace_text);
    return true;
}

void MainWindow::run_search(bool forward, bool from_caret, bool selection_only) {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    if (last_search_pattern_.isEmpty()) {
        find();
        return;
    }

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Search"));
    progress_dialog.setLabelText(QStringLiteral("Searching..."));
    progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };

    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    bool canceled = false;
    const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }

        last_ui_completed = completed;
        ui_timer.restart();
        progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
        const QString label = total > 0
            ? QStringLiteral("Searching... %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
            : QStringLiteral("Searching...");
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress_dialog.wasCanceled()) {
            canceled = true;
            return false;
        }
        return true;
    };

    statusBar()->showMessage(QStringLiteral("Searching..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    progress_dialog.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    qint64 found_offset = -1;
    bool search_canceled = false;
    const bool found = selection_only
        ? hex_view_->find_pattern_in_selection(last_search_pattern_, forward, from_caret, &found_offset, &search_canceled, progress_callback)
        : hex_view_->find_pattern(last_search_pattern_, forward, from_caret, &found_offset, &search_canceled, progress_callback);
    progress_dialog.setValue(1000);
    QApplication::restoreOverrideCursor();
    if (canceled || search_canceled) {
        statusBar()->showMessage(QStringLiteral("Search canceled."), 2500);
        return;
    }
    if (!found) {
        show_search_summary(
            QStringLiteral("No matches found%1.")
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            QStringLiteral("Find"));
        statusBar()->showMessage(QStringLiteral("No matches found."), 2500);
        return;
    }

    show_search_matches(
        QStringLiteral("1 match at 0x%1").arg(found_offset, 8, 16, QChar(u'0')).toUpper(),
        QVector<qint64>{found_offset},
        QStringLiteral("Find"));
    statusBar()->showMessage(QStringLiteral("Match at 0x%1").arg(found_offset, 0, 16), 2500);
}

void MainWindow::run_find_all(bool selection_only) {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    if (last_search_pattern_.isEmpty()) {
        find();
        return;
    }

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Find All"));
    progress_dialog.setLabelText(QStringLiteral("Searching..."));
    progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };

    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    bool canceled = false;
    const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }

        last_ui_completed = completed;
        ui_timer.restart();
        progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
        const QString label = total > 0
            ? QStringLiteral("Searching... %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
            : QStringLiteral("Searching...");
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress_dialog.wasCanceled()) {
            canceled = true;
            return false;
        }
        return true;
    };

    statusBar()->showMessage(QStringLiteral("Searching..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    progress_dialog.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    bool search_canceled = false;
    const QVector<qint64> matches = hex_view_->find_all_patterns(last_search_pattern_, selection_only, &search_canceled, progress_callback);
    progress_dialog.setValue(1000);
    QApplication::restoreOverrideCursor();
    if (canceled || search_canceled) {
        if (!matches.isEmpty()) {
            show_search_matches(
                QStringLiteral("%1 match(es) found before canceling%2.")
                    .arg(matches.size())
                    .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
                matches,
                QStringLiteral("Find"));
            statusBar()->showMessage(QStringLiteral("Search canceled after %1 match(es).").arg(matches.size()), 3000);
        } else {
            statusBar()->showMessage(QStringLiteral("Search canceled."), 2500);
        }
        return;
    }
    if (matches.isEmpty()) {
        show_search_summary(
            QStringLiteral("No matches found%1.")
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            QStringLiteral("Find"));
    } else {
        show_search_matches(
            QStringLiteral("%1 match(es)%2.")
                .arg(matches.size())
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            matches,
            QStringLiteral("Find"));
    }
    if (search_results_dock_ != nullptr) {
        search_results_dock_->raise();
        search_results_dock_->show();
    }
    statusBar()->showMessage(
        matches.isEmpty()
            ? QStringLiteral("No matches found.")
            : QStringLiteral("Found %1 match(es).").arg(matches.size()),
        3000);
}

bool MainWindow::try_parse_hex_bytes(const QString& text, QByteArray& bytes) {
    QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(2);
    }

    normalized.remove(QLatin1Char(' '));
    if (normalized.isEmpty() || normalized.size() % 2 != 0) {
        return false;
    }

    QByteArray parsed;
    parsed.reserve(normalized.size() / 2);
    for (int index = 0; index < normalized.size(); index += 2) {
        bool ok = false;
        const auto byte = static_cast<char>(normalized.mid(index, 2).toUInt(&ok, 16));
        if (!ok) {
            return false;
        }
        parsed.append(byte);
    }

    bytes = parsed;
    return true;
}

bool MainWindow::try_parse_offset(const QString& text, quint64& value) {
    QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    int base = 10;
    if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(2);
        base = 16;
    }

    bool ok = false;
    value = normalized.toULongLong(&ok, base);
    return ok;
}
