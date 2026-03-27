import math
import numpy as np

def gaussian(x, alpha, mu, sigma1, sigma2):
    t = (x - mu) / (sigma1 if x < mu else sigma2)
    return alpha * math.exp(-0.5 * t * t)

def cie_x(l):
    return gaussian(l, 1.056, 599.8, 37.9, 31.0) + \
           gaussian(l, 0.362, 442.0, 16.0, 26.7) + \
           gaussian(l, -0.065, 501.1, 20.4, 26.2)

def cie_y(l):
    return gaussian(l, 0.821, 568.8, 46.9, 40.5) + \
           gaussian(l, 0.286, 530.9, 16.3, 31.1)

def cie_z(l):
    return gaussian(l, 1.217, 437.0, 11.8, 36.0) + \
           gaussian(l, 0.681, 459.0, 26.0, 13.8)

# Visible range
l_min, l_max = 380, 720
step = 1.0
lambdas = np.arange(l_min, l_max + step, step)

# Precompute CMFs
X = np.array([cie_x(l) for l in lambdas])
Y = np.array([cie_y(l) for l in lambdas])
Z = np.array([cie_z(l) for l in lambdas])

# Precompute basis spectra (Gaussians)
# Blue, Green, Red centers (matching GPU code exactly)
centers = [440, 545, 630]
sigmas = [40, 50, 60]

def get_basis_spectrum(l, center, sigma):
    return math.exp(-0.5 * ((l - center) / sigma)**2)

basis_B = np.array([get_basis_spectrum(l, centers[0], sigmas[0]) for l in lambdas])
basis_G = np.array([get_basis_spectrum(l, centers[1], sigmas[1]) for l in lambdas])
basis_R = np.array([get_basis_spectrum(l, centers[2], sigmas[2]) for l in lambdas])

# We want to find weights wB, wG, wR such that
# (wB*basis_B + wG*basis_G + wR*basis_R) integrated with CMFs matches D65 XYZ.
# D65 White XYZ (normalized to Y=1.0)
target_XYZ = np.array([0.9504, 1.0000, 1.0888])

# Integration matrix M (3x3)
# M[0, 0] = Integral(basis_B * CMF_X)
# ...
M = np.zeros((3, 3))
for i, basis in enumerate([basis_R, basis_G, basis_B]): # Order: R, G, B
    M[0, i] = np.sum(basis * X) * step
    M[1, i] = np.sum(basis * Y) * step
    M[2, i] = np.sum(basis * Z) * step

# Y integral of CMF_Y is about 106.856.
# Our GPU code divides by this 106.856.
# So we need M_gpu = M / 106.856.
M_gpu = M / 106.9119 # Use the integral Y from previous script

print("Integration Matrix (GPU normalized):")
print(M_gpu)

# Solve for weights
try:
    weights = np.linalg.solve(M_gpu, target_XYZ)
    print("\nOptimal Weights for RGB to Spectrum (R, G, B):")
    print(weights)
except Exception as e:
    print(f"Error solving: {e}")

# Verify
result_XYZ = M_gpu @ weights
print(f"\nResulting XYZ: {result_XYZ}")
print(f"Target XYZ:    {target_XYZ}")

# Also check for individual colors
print("\nIndividual Color Check:")
colors = {
    "Pure Red": [1, 0, 0],
    "Pure Green": [0, 1, 0],
    "Pure Blue": [0, 0, 1]
}

def xyz_to_rgb(xyz):
    x, y, z = xyz
    r =  3.2406 * x - 1.5372 * y - 0.4986 * z
    g = -0.9689 * x + 1.8758 * y + 0.0415 * z
    b =  0.0557 * x - 0.2040 * y + 1.0570 * z
    return np.array([r, g, b])

for name, rgb in colors.items():
    spec_weights = np.array(rgb) * weights
    xyz = M_gpu @ spec_weights
    res_rgb = xyz_to_rgb(xyz)
    print(f"{name}: Input RGB {rgb} -> Result RGB {res_rgb}")

# We might need a small adjustment because the basis spectra are not orthogonal.
