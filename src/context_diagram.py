"""
Context diagram for the fall-detection & caregiver-alert system.

Legend:
  - solid box    = the system itself (one block, nothing subdivided)
                   and other required equipment (computer, dashboard)
  - dashed box   = the system boundary (drawn around the one system
                   block, per the guide's figure)
  - dashed ellipses = stakeholders (people), connected by dashed lines
                      to whichever equipment they care about
  - solid arrows = labeled data/signal flow between equipment
  - dashed lines = unlabeled association between a stakeholder and
                    the equipment relevant to them (no data label needed)

Outputs: context_diagram.pdf
"""

import matplotlib.pyplot as plt
import matplotlib.patches as patches

fig, ax = plt.subplots(figsize=(12.5, 5.6))
ax.set_xlim(-5, 205)
ax.set_ylim(8, 96)
ax.set_aspect("equal")
ax.axis("off")

BOX_FC = "white"
BOX_EC = "black"
LW = 1.3


def solid_box(xy, w, h, text, fontsize=10):
    """Solid rectangle for required equipment."""
    x, y = xy
    rect = patches.FancyBboxPatch(
        (x, y), w, h,
        boxstyle="round,pad=0,rounding_size=2",
        linewidth=LW, edgecolor=BOX_EC, facecolor=BOX_FC, zorder=2,
    )
    ax.add_patch(rect)
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
             fontsize=fontsize, zorder=3)
    return (x, y, w, h)


def dashed_container(xy, w, h, label):
    """Dashed rectangle for the system boundary."""
    x, y = xy
    rect = patches.FancyBboxPatch(
        (x, y), w, h,
        boxstyle="round,pad=0,rounding_size=2",
        linewidth=LW, edgecolor=BOX_EC, facecolor="none",
        linestyle=(0, (5, 3)), zorder=1,
    )
    ax.add_patch(rect)
    ax.text(x + 4, y + h - 6, label, ha="left", va="center",
             fontsize=10, fontweight="bold", zorder=3)
    return (x, y, w, h)


def dashed_ellipse(xy, w, h, text, fontsize=10):
    """Dashed ellipse for a stakeholder (person / entity)."""
    x, y = xy  # center
    ell = patches.Ellipse(
        (x, y), w, h,
        linewidth=LW, edgecolor=BOX_EC, facecolor="white",
        linestyle=(0, (5, 3)), zorder=2,
    )
    ax.add_patch(ell)
    ax.text(x, y, text, ha="center", va="center",
             fontsize=fontsize, zorder=3)
    return (x, y, w, h)


def solid_arrow(start, end, label=None, label_offset=(0, 3),
                bidirectional=False, fontsize=9):
    """Solid labeled arrow for a data/signal flow."""
    style = "<->" if bidirectional else "->"
    arrow = patches.FancyArrowPatch(
        start, end, arrowstyle=style, mutation_scale=14,
        linewidth=LW, color="black", zorder=2,
    )
    ax.add_patch(arrow)
    if label:
        mx, my = (start[0] + end[0]) / 2, (start[1] + end[1]) / 2
        ax.text(mx + label_offset[0], my + label_offset[1], label,
                 ha="center", va="center", fontsize=fontsize, zorder=3,
                 bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="none"))


def dashed_line(start, end):
    """Unlabeled dashed association line (stakeholder <-> equipment)."""
    ax.plot([start[0], end[0]], [start[1], end[1]],
            linestyle=(0, (4, 3)), linewidth=LW, color="black", zorder=1)


# ---------------------------------------------------------------------
# System boundary containing one undivided block. The watch and custom
# PCB both live inside this block
# ---------------------------------------------------------------------
dashed_container((40, 40), 50, 50, "System Boundary")

sys_x, sys_y, sys_w, sys_h = solid_box(
    (48, 46), 34, 29, "Fall Detection\nSystem\n(watch + custom PCB)")
sys_mid_y = sys_y + sys_h / 2

# ---------------------------------------------------------------------
# Computer (MQTT broker) and Dashboard: two separate pieces of required
# equipment, outside the boundary
# ---------------------------------------------------------------------
comp_x, comp_y, comp_w, comp_h = solid_box(
    (100, 54), 34, 13, "Computer\n(MQTT broker)")
dash_x, dash_y, dash_w, dash_h = solid_box(
    (100, 29), 34, 13, "Dashboard\n(with alert system)")

solid_arrow(
    (sys_x + sys_w, sys_mid_y),
    (comp_x, comp_y + comp_h / 2),
    label="confirmed alert\n(MQTT)",
    label_offset=(0, 6), fontsize=8.5,
)

solid_arrow(
    (comp_x + comp_w / 2, comp_y),
    (dash_x + dash_w / 2, dash_y + dash_h),
    label="push alert",
    label_offset=(14, 0), fontsize=8.5,
)

# ---------------------------------------------------------------------
# Wearer: dashed ellipse, dashed (unlabeled) line into the system block
# ---------------------------------------------------------------------
wearer_x, wearer_y = 10, sys_mid_y
dashed_ellipse((wearer_x, wearer_y), 20, 15, "Wearer")
dashed_line((wearer_x + 10, wearer_y), (sys_x, sys_mid_y))

ax.text(wearer_x + 15, wearer_y + 10, "motion /\ncancel", ha="center",
        va="center", fontsize=8, style="italic")

# ---------------------------------------------------------------------
# Caregiver / Family and Care Facility: two independent stakeholders,
# each with its own dashed line straight from the dashboard
# ---------------------------------------------------------------------
caregiver_x, caregiver_y = 172, 49
carefac_x, carefac_y = 172, 19

dashed_ellipse((caregiver_x, caregiver_y), 26, 15, "Caregiver /\nFamily")
dashed_ellipse((carefac_x, carefac_y), 26, 15, "Care\nFacility")

dashed_line((dash_x + dash_w, dash_y + dash_h * 0.85),
            (caregiver_x - 13, caregiver_y))
dashed_line((dash_x + dash_w, dash_y + dash_h * 0.15),
            (carefac_x - 13, carefac_y))

plt.tight_layout()
plt.savefig("../figures/generated/context_diagram.pdf", bbox_inches="tight")