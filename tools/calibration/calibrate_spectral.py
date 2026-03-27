import math

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

def xyz_to_rgb(x, y, z):
    # sRGB D65 Conversion Matrix
    r =  3.2406 * x - 1.5372 * y - 0.4986 * z
    g = -0.9689 * x + 1.8758 * y + 0.0415 * z
    b =  0.0557 * x - 0.2040 * y + 1.0570 * z
    return r, g, b

# Numerical Integration parameters
l_min = 380.0
l_max = 720.0
step = 0.1
steps = int((l_max - l_min) / step)

print(f"--- CIE 1931 CMF Calibration (Range: {l_min}-{l_max}nm, Step: {step}nm) ---")

sum_x = 0.0
sum_y = 0.0
sum_z = 0.0

for i in range(steps + 1):
    l = l_min + i * step
    sum_x += cie_x(l) * step
    sum_y += cie_y(l) * step
    sum_z += cie_z(l) * step

print(f"Integral X: {sum_x:.4f}")
print(f"Integral Y: {sum_y:.4f}")
print(f"Integral Z: {sum_z:.4f}")

# Target: Y = 1.0 for a flat spectrum of Value = 1.0
# The Riemann sum in GPU code is: (DomainWidth / N) * sum(Value * CMF)
# For N=4, Value=1, it is (DomainWidth / 4) * sum(CMF)
# Our integral represents sum(CMF * delta_lambda)
# So if we have N=4 samples, the effective delta_lambda is (DomainWidth / 4)
# Normalization should ensure (DomainWidth / 4) * normalization * sum(CMF) / Y_scale = 1.0?
# No, let's keep it simpler: 
# XYZ = (1.0 / Y_Integral) * (Integral_sum_of_sampled_points)
# Integral_sum_of_sampled_points = sum(Value * CMF * delta_lambda) where delta_lambda = DomainWidth / N
# So XYZ = (DomainWidth / (N * Y_Integral)) * sum(Value * CMF)

y_integral = sum_y
normalization = (l_max - l_min) / (4.0 * y_integral)

print(f"\nRecommended Normalization Factor for N=4: {normalization:.8f}")

# Verify white point
nx = sum_x * normalization / ((l_max - l_min) / 4.0)
ny = sum_y * normalization / ((l_max - l_min) / 4.0)
nz = sum_z * normalization / ((l_max - l_min) / 4.0)

# Wait, the GPU logic is (x, y, z) * normalization.
# Let's re-simulate the GPU logic exactly:
# N=4 samples at 422.5, 507.5, 592.5, 677.5 (approximate stratified bins)
domain = l_max - l_min
bin = domain / 4.0
samples = [l_min + 0.5 * bin, l_min + 1.5 * bin, l_min + 2.5 * bin, l_min + 3.5 * bin]
print(f"Simulation wavelengths: {samples}")

sx, sy, sz = 0, 0, 0
for l in samples:
    sx += cie_x(l)
    sy += cie_y(l)
    sz += cie_z(l)

final_x = sx * normalization
final_y = sy * normalization
final_z = sz * normalization

print(f"Final XYZ for Flat White: ({final_x:.4f}, {final_y:.4f}, {final_z:.4f})")
r, g, b = xyz_to_rgb(final_x, final_y, final_z)
print(f"Final sRGB for Flat White: ({r:.4f}, {g:.4f}, {b:.4f})")

# If R is too high (1.7), it means X/Y balance is wrong or normalization is too large.
# Standard normalization for CIE Y is sum(y * step) = 1.0 (or 100).
# Let's see.
