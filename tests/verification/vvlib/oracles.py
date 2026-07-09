"""Analytical oracles for the verification cases (doc/VV/vv_rationale.md, section 3)."""

import numpy as np

from .constants import MU_BLOOD


def hagen_poiseuille_dp(Q, D, L, mu=MU_BLOOD):
    """Pressure drop of laminar developed pipe flow: dp = 128 mu L Q / (pi D^4)."""
    return 128.0 * mu * L * Q / (np.pi * D**4)


def poiseuille_profile(r, R, Q):
    """Axial velocity at radius r for volume flow Q in a pipe of radius R."""
    u_mean = Q / (np.pi * R**2)
    return 2.0 * u_mean * (1.0 - (np.asarray(r) / R) ** 2)


def murray_split(diameters):
    """Murray-law outlet flow fractions: Q_i ~ D_i^3."""
    d3 = np.asarray(diameters, dtype=float) ** 3
    return d3 / d3.sum()


def poiseuille_resistance(D, L, mu=MU_BLOOD):
    """Laminar flow resistance of a tube segment: R = 128 mu L / (pi D^4)."""
    return 128.0 * mu * L / (np.pi * D**4)


def resistance_split(diameters, lengths, mu=MU_BLOOD):
    """Flow fractions of parallel outlet branches at equal outlet pressure.

    Branches share one junction node; Q_i ~ 1/R_i.
    """
    g = np.array([1.0 / poiseuille_resistance(d, l, mu)
                  for d, l in zip(diameters, lengths)])
    return g / g.sum()


def observed_order(f_coarse, f_mid, f_fine, refinement_ratio):
    """Observed order of convergence from three solutions at constant ratio r."""
    return np.log(abs(f_coarse - f_mid) / abs(f_mid - f_fine)) / np.log(refinement_ratio)
