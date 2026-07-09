"""Blood properties shared by all verification oracles.

Values match the solver: BLOOD_DENSITY in globals.h:53 and the Newtonian
kinematic viscosity nuInf in hemoFlow.cpp:67 (the active BackgroundDynamics is
a regularized BGK at sim.omega, i.e. Newtonian; the Carreau parameters are set
but not used by it).
"""

RHO_BLOOD = 1055.0        # [kg/m^3]  (globals.h BLOOD_DENSITY)
NU_BLOOD = 3.22e-6        # [m^2/s]   (hemoFlow.cpp nuInf)
MU_BLOOD = RHO_BLOOD * NU_BLOOD   # [Pa s]
