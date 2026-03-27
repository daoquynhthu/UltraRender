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
    r =  3.2406 * x - 1.5372 * y - 0.4986 * z
    g = -0.9689 * x + 1.8758 * y + 0.0415 * z
    b =  0.0557 * x - 0.2040 * y + 1.0570 * z
    return r, g, b

def test_wavelength(lambda_val):
    x = cie_x(lambda_val)
    y = cie_y(lambda_val)
    z = cie_z(lambda_val)
    r, g, b = xyz_to_rgb(x, y, z)
    print(f"Lambda: {lambda_val}nm -> XYZ: ({x:.3f}, {y:.3f}, {z:.3f}) -> RGB: ({r:.3f}, {g:.3f}, {b:.3f})")

print("--- Spectral to RGB Mapping Debug ---")
test_wavelength(450) # Should be Blue
test_wavelength(550) # Should be Green
test_wavelength(650) # Should be Red

print("\n--- White Point Test (Flat Spectrum) ---")
# Integrate over 4 wavelengths like in the GPU code
lambdas = [400, 500, 600, 700]
sx = sum(cie_x(l) for l in lambdas)
sy = sum(cie_y(l) for l in lambdas)
sz = sum(cie_z(l) for l in lambdas)
# Apply normalization similar to GPU (width / 4) / 106.856
norm = (400 / 4) / 106.856
r, g, b = xyz_to_rgb(sx * norm, sy * norm, sz * norm)
print(f"Flat Spectrum White -> RGB: ({r:.3f}, {g:.3f}, {b:.3f})")
