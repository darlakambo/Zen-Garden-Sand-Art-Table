"""
- virtiual sand box (24x24 grid), draws current position of ball and path it travelled
- faint grid lines every 4 units (6 divisions across 24)
- coordinate label below canvas
"""

from collections import deque
from PySide6.QtWidgets import QWidget
from PySide6.QtCore import Qt, QPointF, QRectF
from PySide6.QtGui import QPainter, QPen, QColor, QBrush, QFont

#/////////////////////////////////////////////////////////////////////////////////////////////////////
#============================== Constants and Configuration ============================================
#/////////////////////////////////////////////////////////////////////////////////////////////////////

# Grid dimensions (matches GRID_MAX in Arduino firmware)
GRID_SIZE = 24

# Maximum number of positions kept in the path history
MAX_TRAIL_POINTS = 2000

#/////////////////////////////////////////////////////////////////////////////////////////////////////
#============================== SandCanvas Widget Definition ===========================================
#/////////////////////////////////////////////////////////////////////////////////////////////////////

class SandCanvas(QWidget):
    # Display 24x24 grid with ball position and travel path

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(300, 300)

        self._trail: deque[QPointF] = deque(maxlen=MAX_TRAIL_POINTS)
        self._current_pos: QPointF | None = None

        # Colors
        self._sand_color   = QColor("#F5DEB3")   # beige sand box background
        self._trail_color  = QColor("#A0784A")   # brown travel path (darker than sand)
        self._ball_color   = QColor("#3A2A10")   # dark ball marker
        self._grid_color   = QColor("#D4B87A")   # faint grid lines
        self._border_color = QColor("#8B6914")   

        self._margin = 20

    #### Functions called by main window when new data arrives or mode changes ####

    def update_position(self, x: float, y: float):
        # Add new position to the trail and update current position
        pt = QPointF(float(x), float(y))
        self._trail.append(pt)
        self._current_pos = pt
        self.update()

    def clear_trail(self):
        # Clear the trail and reset current position
        self._trail.clear()
        self._current_pos = None
        self.update()

    #### Coordinate conversions and drawing ####

    def _sandbox_rect(self) -> QRectF:
        # Calculate the largest square area within the widget, leave margins, and return for drawing the sand box
        m           = self._margin
        available_w = self.width()  - 2 * m
        available_h = self.height() - 2 * m
        side        = min(available_w, available_h)
        x           = m + (available_w - side) / 2
        y           = m + (available_h - side) / 2
        return QRectF(x, y, side, side)

    def _grid_to_px(self, pt: QPointF) -> QPointF:
        # Convert grid coordinates (0 to GRID_SIZE) to pixel coordinates within the sand box rectangle
        rect = self._sandbox_rect()
        px_x = rect.x() + (pt.x() / GRID_SIZE) * rect.width()
        px_y = rect.y() + ((GRID_SIZE - pt.y()) / GRID_SIZE) * rect.height()
        return QPointF(px_x, px_y)

    #### Main Drawing Functions ####

    def paintEvent(self, event):
        # Draw the sand box, grid, path, and ball
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        rect = self._sandbox_rect()

        # Sand background with rounded corners
        painter.setPen(Qt.NoPen)
        painter.setBrush(QBrush(self._sand_color))
        painter.drawRoundedRect(rect, 10, 10)

        # Faint dotted grid (one line every 4 grid units)
        grid_pen = QPen(self._grid_color, 0.8, Qt.DotLine)      
        grid_pen.setDashPattern([2, 4])                         
        painter.setPen(grid_pen)                                # Draw grid lines with dotted pattern
        for i in range(4, GRID_SIZE, 4):                        # Draw lines at 4, 8, 12, 16, 20
            painter.drawLine(
                self._grid_to_px(QPointF(i, 0)),
                self._grid_to_px(QPointF(i, GRID_SIZE)),
            )
            painter.drawLine(
                self._grid_to_px(QPointF(0, i)),
                self._grid_to_px(QPointF(GRID_SIZE, i)),
            )

        # Border
        painter.setPen(QPen(self._border_color, 1.5))
        painter.setBrush(Qt.NoBrush)
        painter.drawRoundedRect(rect, 10, 10)                   # Draw border with same rounded corners as background

        # Travel path
        if len(self._trail) >= 2:
            trail_list = list(self._trail)
            painter.setPen(QPen(self._trail_color, 2.0, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))   # Draw travel path with solid line, rounded edges
            for i in range(1, len(trail_list)):                 
                painter.drawLine(
                    self._grid_to_px(trail_list[i - 1]),        # Convert grid coordinates to pixel coordinates for drawing
                    self._grid_to_px(trail_list[i]),            # Draw line segment from previous point to current point in the trail
                )

        # Ball
        if self._current_pos is not None:
            center      = self._grid_to_px(self._current_pos)   # Convert current grid position to pixel coordinates for drawing the ball
            ball_radius = max(5.0, rect.width() / 45)           # Ball radius scales with canvas size, but has minimum size
            painter.setPen(QPen(QColor("#1A1A1A"), 1.0))
            painter.setBrush(QBrush(self._ball_color))
            painter.drawEllipse(center, ball_radius, ball_radius)

        # Coordinate label below the canvas
        if self._current_pos is not None:
            label = f"({int(self._current_pos.x())}, {int(self._current_pos.y())})"     # Format current grid coordinates as text label
            painter.setFont(QFont("Segoe UI", 8))
            painter.setPen(QPen(QColor("#6B4F2A")))
            painter.drawText(                                                           # Draw the coordinate label centered below the sand box rectangle
                QRectF(rect.x(), rect.y() + rect.height() + 4, rect.width(), 18),
                Qt.AlignCenter,
                label,
            )

        painter.end()                                           # Finish drawing