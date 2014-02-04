#include "view.h"

#include "ohlc_standardizer.h"
#include "ohlc_shrinker.h"
#include "ohlc_random_generator.h"

#include "graph_event_controller.h"

#include "candle.h"
#include "grid.h"

#ifndef QT_NO_PRINTER
#include <QPrinter>
#include <QPrintDialog>
#endif
#ifndef QT_NO_OPENGL
#include <QtOpenGL>
#else
#include <QtWidgets>
#endif
#include <qmath.h>

void GraphicsView::wheelEvent(QWheelEvent *e) {
	// if (e->modifiers() & Qt::ControlModifier) {
	view->zoom(e->delta(), e->x());
	e->accept();
}

void GraphicsView::resizeEvent(QResizeEvent*) {
	emit resized();
}

View::View(const QString &name, QWidget *parent) : QFrame(parent) {
	source = NULL;

	setFrameStyle(Sunken | StyledPanel);
	graphicsView = new GraphicsView(this);
	graphicsView->setRenderHint(QPainter::Antialiasing, false);
	graphicsView->setDragMode(QGraphicsView::RubberBandDrag);
	graphicsView->setOptimizationFlags(QGraphicsView::DontSavePainterState);
	graphicsView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
	graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

	int size = style()->pixelMetric(QStyle::PM_ToolBarIconSize);
	QSize iconSize(size, size);

	QToolButton *zoomInIcon = new QToolButton;
	QToolButton *zoomOutIcon = new QToolButton;

	// Zoom slider layout
	QVBoxLayout *zoomSliderLayout = new QVBoxLayout;
	zoomSliderLayout->addWidget(zoomInIcon);
	zoomSliderLayout->addWidget(zoomOutIcon);

	resetButton = new QToolButton;
	resetButton->setText(tr("0"));

	// Label layout
	QHBoxLayout *labelLayout = new QHBoxLayout;
	label = new QLabel(name);
	label2 = new QLabel(tr("Pointer Mode"));
	selectModeButton = new QToolButton;
	selectModeButton->setText(tr("Select"));
	selectModeButton->setCheckable(true);
	selectModeButton->setChecked(true);
	dragModeButton = new QToolButton;
	dragModeButton->setText(tr("Drag"));
	dragModeButton->setCheckable(true);
	dragModeButton->setChecked(false);
	antialiasButton = new QToolButton;
	antialiasButton->setText(tr("Antialiasing"));
	antialiasButton->setCheckable(true);
	antialiasButton->setChecked(false);
	openGlButton = new QToolButton;
	openGlButton->setText(tr("OpenGL"));
	openGlButton->setCheckable(true);
#ifndef QT_NO_OPENGL
	openGlButton->setEnabled(QGLFormat::hasOpenGL());
#else
	openGlButton->setEnabled(false);
#endif
	printButton = new QToolButton;
	// printButton->setIcon(QIcon(QPixmap(":/fileprint.png")));

	QButtonGroup *pointerModeGroup = new QButtonGroup;
	pointerModeGroup->setExclusive(true);
	pointerModeGroup->addButton(selectModeButton);
	pointerModeGroup->addButton(dragModeButton);

	labelLayout->addWidget(label);
	labelLayout->addStretch();
	labelLayout->addWidget(label2);
	labelLayout->addWidget(selectModeButton);
	labelLayout->addWidget(dragModeButton);
	labelLayout->addStretch();
	labelLayout->addWidget(antialiasButton);
	labelLayout->addWidget(openGlButton);
	labelLayout->addWidget(printButton);

	QGridLayout *topLayout = new QGridLayout;
	topLayout->addLayout(labelLayout, 0, 0);
	topLayout->addWidget(graphicsView, 1, 0);
	topLayout->addLayout(zoomSliderLayout, 1, 1);
	topLayout->addWidget(resetButton, 2, 1);
	setLayout(topLayout);

	connect(resetButton, SIGNAL(clicked()), this, SLOT(resetView()));
	connect(selectModeButton, SIGNAL(toggled(bool)), this, SLOT(togglePointerMode()));
	connect(dragModeButton, SIGNAL(toggled(bool)), this, SLOT(togglePointerMode()));
	connect(antialiasButton, SIGNAL(toggled(bool)), this, SLOT(toggleAntialiasing()));
	connect(openGlButton, SIGNAL(toggled(bool)), this, SLOT(toggleOpenGL()));
	// connect(zoomInIcon, SIGNAL(clicked()), this, SLOT(zoom()));
	// connect(zoomOutIcon, SIGNAL(clicked()), this, SLOT(zoomOut()));
	connect(printButton, SIGNAL(clicked()), this, SLOT(print()));

	scene = new QGraphicsScene;
	view()->setScene(scene);

	resetView();

	connect(view(), SIGNAL(resized()), this, SLOT(redraw()));
}

void View::candleEntered(QDateTime start) {
	// TODO
	qDebug() << "candle entered:" << start;
}

void View::candleLeft() {
	qDebug() << "candle exited";
}

QGraphicsView *View::view() const {
	return static_cast<QGraphicsView *>(graphicsView);
}

void View::resetView() {
	// viewEnd = source->getMaximum();
	// viewAtom = source->getQuantumSeconds();
	zoomLevel = 0.0;
	setupMatrix();
	graphicsView->ensureVisible(QRectF(0, 0, 0, 0));

	if (source) {
		//double tickCount = (float)(source->getMaximum().toTime_t() - source->getMinimum().toTime_t()) / source->getQuantumSeconds();

		//double shownTickCount = tickCount / exp(zoomLevel);
		//if (shownTickCount < 10) shownTickCount = 10;

		viewBegin = source->getMinimum();
		viewEnd = source->getMaximum();
		//viewEnd = viewBegin.addSecs(source->getQuantumSeconds() * round(shownTickCount));
	}

	redraw();
}

void View::setupMatrix() {
	QMatrix matrix; // identity
	graphicsView->setMatrix(matrix);
}

void View::togglePointerMode() {
	graphicsView->setDragMode(selectModeButton->isChecked()
			? QGraphicsView::RubberBandDrag
			: QGraphicsView::ScrollHandDrag);
	graphicsView->setInteractive(selectModeButton->isChecked());
}

void View::toggleOpenGL() {
#ifndef QT_NO_OPENGL
	graphicsView->setViewport(openGlButton->isChecked() ? new QGLWidget(QGLFormat(QGL::SampleBuffers)) : new QWidget);
#endif
}

void View::toggleAntialiasing() {
	graphicsView->setRenderHint(QPainter::Antialiasing, antialiasButton->isChecked());
}

void View::print() {
#if !defined(QT_NO_PRINTER) && !defined(QT_NO_PRINTDIALOG)
	QPrinter printer;
	QPrintDialog dialog(&printer, this);
	if (dialog.exec() == QDialog::Accepted) {
		QPainter painter(&printer);
		graphicsView->render(&painter);
	}
#endif
}

void View::zoom(int level, int x) {
	GraphRanges ranges;
	ranges.width = view()->width();
	ranges.height = view()->height();
	ranges.start = viewBegin;
	ranges.end = viewEnd;

	double zoomBefore = zoomLevel;
	double zoomAfter = zoomLevel + level * 0.001;

	qDebug() << "scroll: x=" << x << "time=" << ranges.getXTime(x);

	double secondsWidthBase = source->getMaximum().toTime_t() - source->getMinimum().toTime_t();
	double secondsWidthNow = secondsWidthBase / exp(zoomBefore);
	double newSecondsWidth = secondsWidthBase / exp(zoomAfter);

	double newBeginT = 
		((double)ranges.getXTime(x).toTime_t())
		-
			(
				((double)ranges.getXTime(x).toTime_t() - (double)viewBegin.toTime_t())
				* (newSecondsWidth) / (secondsWidthNow)
			);

	QDateTime newBegin = QDateTime::fromTime_t(newBeginT);
	if (newBegin < source->getMinimum()) newBegin = source->getMinimum();
	if (newBegin.addSecs(secondsWidthNow) > source->getMaximum()) newBegin = source->getMaximum().addSecs(-secondsWidthNow);

	QDateTime newEnd = newBegin.addSecs(secondsWidthNow);

	if (zoomAfter <= 0) {
		// TODO: or <10 candles shown then
		zoomLevel = 0;
	} else {
		// commit
		viewBegin = newBegin;
		viewEnd = newEnd;

		qDebug() << "new view begin=" << viewBegin << "end=" << viewEnd;

		zoomLevel = zoomAfter;

	//	zoomLevel += level * 0.001;

		redraw();
	}
}

const float VIEW_MARGIN = 0.2;

void View::redraw() {
	scene->clear();

	if (!source) {
		qDebug() << "redrawing with NULL source, drawing nothing.";
		return;
	}

	if (source->isEmpty()) {
		qDebug() << "source is empty...";
		return;
	}

	// QDateTime viewBegin = source->getMinimum();
	// QDateTime viewEnd = viewBegin;

	int viewAtom = source->getQuantumSeconds();
	qDebug() << "view start:" << viewBegin << "view end:" << viewEnd << "view atom:" << viewAtom;

	GraphRanges ranges;
	ranges.width = view()->width();
	ranges.height = view()->height();
	ranges.start = viewBegin;
	ranges.end = viewEnd;

	OHLCProvider* provider = new OHLCShrinker(source, viewBegin, viewEnd, viewAtom);
	OHLCStandardizer* standardized = new OHLCStandardizer(new OHLCShrinker(source, viewBegin, viewEnd, viewAtom));

	float low = standardized->getSourceClosure().low;
	float high = standardized->getSourceClosure().high;

	ranges.priceLow = low - (high - low) * (VIEW_MARGIN / 2);
	ranges.priceHigh = high + (high - low) * (VIEW_MARGIN / 2);

	scene->addItem(new Grid(ranges));

	// Populate scene
	int nitems = 0;

	GraphEventController* controller = new GraphEventController();
	connect(controller, SIGNAL(candleEntered(QDateTime)), this, SLOT(candleEntered(QDateTime)));
	connect(controller, SIGNAL(candleLeft()), this, SLOT(candleLeft()));

	int quantum = provider->getQuantumSeconds();

	for (QDateTime start = ranges.start; start < ranges.end; start = start.addSecs(quantum)) {
		OHLC tick;
		if (provider->tryGetData(start, tick)) {
			float width = ranges.getTimeSpanWidth(quantum);
			Candle *item = new Candle(start, tick, width, ranges, controller);
			item->setPos(QPointF(ranges.getTimeX(start), 0));
			scene->addItem(item);
			++nitems;
		}
	}

	qDebug() << "Scene created with" << nitems << "ticks";
}

void View::changeDataSource(OHLCProvider* source) {
	this->source = source;
	resetView();
}
