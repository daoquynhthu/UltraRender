from __future__ import annotations

import ctypes
import os
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Optional


class AovType(IntEnum):
    BEAUTY = 0
    NORMAL = 1
    ALBEDO = 2
    DEPTH = 3
    UV = 4
    MOTION_VECTOR = 5


class SessionState(IntEnum):
    EMPTY = 0
    READY = 1
    RUNNING = 2
    PAUSED = 3
    CANCELED = 4


class LogLevel(IntEnum):
    TRACE = 0
    DEBUG = 1
    INFO = 2
    WARN = 3
    ERROR = 4
    FATAL = 5


class MaterialType(IntEnum):
    LAMBERTIAN = 0
    METAL = 1
    DIELECTRIC = 2
    LIGHT = 3


class _Progress(ctypes.Structure):
    _fields_ = [
        ("spp", ctypes.c_int),
        ("state", ctypes.c_int),
        ("has_scene", ctypes.c_int),
    ]


class _SpectralConfig(ctypes.Structure):
    _fields_ = [
        ("domain_bins", ctypes.c_uint64),
        ("packet_lanes", ctypes.c_int),
        ("max_resident_mb", ctypes.c_int),
        ("queue_capacity", ctypes.c_int),
        ("max_trace_depth", ctypes.c_int),
    ]


class _WaveOpticsConfig(ctypes.Structure):
    _fields_ = [
        ("mode", ctypes.c_int),
        ("camera_diffraction_enabled", ctypes.c_int),
        ("coherent_field_enabled", ctypes.c_int),
        ("partial_coherence_enabled", ctypes.c_int),
        ("diffractive_materials_enabled", ctypes.c_int),
        ("fluorescence_enabled", ctypes.c_int),
        ("specular_manifold_enabled", ctypes.c_int),
        ("local_fullwave_enabled", ctypes.c_int),
        ("experimental_allow_preview_degradation", ctypes.c_int),
    ]


class _IntegratorConfig(ctypes.Structure):
    _fields_ = [
        ("mode", ctypes.c_int),
        ("sampler", ctypes.c_int),
        ("quality_preset", ctypes.c_int),
        ("allow_biased_reuse", ctypes.c_int),
        ("path_guiding_enabled", ctypes.c_int),
        ("path_guiding_light_mixture", ctypes.c_float),
        ("path_guiding_learning_rate", ctypes.c_float),
        ("path_guiding_min_weight", ctypes.c_float),
        ("restir_di_enabled", ctypes.c_int),
        ("restir_di_temporal_reuse", ctypes.c_int),
        ("restir_di_spatial_reuse", ctypes.c_int),
        ("restir_di_unbiased", ctypes.c_int),
        ("restir_di_max_history", ctypes.c_int),
        ("specular_manifold_enabled", ctypes.c_int),
        ("specular_manifold_max_events", ctypes.c_int),
        ("specular_manifold_tolerance", ctypes.c_float),
        ("specular_manifold_newton_iterations", ctypes.c_int),
        ("mlt_enabled", ctypes.c_int),
        ("mlt_chain_count", ctypes.c_int),
        ("mlt_mutations_per_chain", ctypes.c_int),
        ("mlt_large_step_probability", ctypes.c_float),
        ("mlt_small_step_sigma", ctypes.c_float),
        ("mlt_seed", ctypes.c_uint32),
    ]


@dataclass(frozen=True)
class Progress:
    spp: int
    state: SessionState
    has_scene: bool


def _candidate_library_paths() -> list[Path]:
    here = Path(__file__).resolve().parent
    env_path = os.environ.get("PYURE_NATIVE")
    candidates: list[Path] = []
    if env_path:
        candidates.append(Path(env_path))
    candidates.extend([
        here / "pyure_native.dll",
        here.parent / "build_modular" / "pyure" / "Release" / "pyure_native.dll",
        here.parent / "build_modular" / "pyure" / "pyure_native.dll",
    ])
    return candidates


def _load_native() -> ctypes.CDLL:
    for path in _candidate_library_paths():
        if path.exists():
            lib = ctypes.CDLL(str(path))
            _configure_abi(lib)
            return lib
    searched = "\n".join(str(path) for path in _candidate_library_paths())
    raise RuntimeError(f"pyure_native.dll not found. Searched:\n{searched}")


def _configure_abi(lib: ctypes.CDLL) -> None:
    lib.ure_set_min_log_level.argtypes = [ctypes.c_int]
    lib.ure_session_create.restype = ctypes.c_void_p
    lib.ure_session_create_config.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.ure_session_create_config.restype = ctypes.c_void_p
    lib.ure_session_create_spectral_config.argtypes = [ctypes.POINTER(_SpectralConfig)]
    lib.ure_session_create_spectral_config.restype = ctypes.c_void_p
    lib.ure_session_create_wave_config.argtypes = [
        ctypes.POINTER(_SpectralConfig),
        ctypes.POINTER(_WaveOpticsConfig),
    ]
    lib.ure_session_create_wave_config.restype = ctypes.c_void_p
    lib.ure_session_create_integrator_config.argtypes = [
        ctypes.POINTER(_SpectralConfig),
        ctypes.POINTER(_WaveOpticsConfig),
        ctypes.POINTER(_IntegratorConfig),
    ]
    lib.ure_session_create_integrator_config.restype = ctypes.c_void_p
    lib.ure_session_destroy.argtypes = [ctypes.c_void_p]
    lib.ure_session_load_scene_file.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.ure_session_load_scene_file.restype = ctypes.c_int
    lib.ure_session_start.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.ure_session_start.restype = ctypes.c_int
    lib.ure_session_render_pass.argtypes = [ctypes.c_void_p]
    lib.ure_session_render_pass.restype = ctypes.c_int
    lib.ure_session_pause.argtypes = [ctypes.c_void_p]
    lib.ure_session_resume.argtypes = [ctypes.c_void_p]
    lib.ure_session_cancel.argtypes = [ctypes.c_void_p]
    lib.ure_session_reset_accumulation.argtypes = [ctypes.c_void_p]
    lib.ure_session_update_camera.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_float,
    ]
    lib.ure_session_update_camera.restype = ctypes.c_int
    lib.ure_session_update_instance_transform.argtypes = [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
    ]
    lib.ure_session_update_instance_transform.restype = ctypes.c_int
    lib.ure_session_update_material.argtypes = [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_float,
        ctypes.c_float,
        ctypes.POINTER(ctypes.c_float),
    ]
    lib.ure_session_update_material.restype = ctypes.c_int
    lib.ure_session_update_material_texture.argtypes = [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_float),
    ]
    lib.ure_session_update_material_texture.restype = ctypes.c_int
    lib.ure_session_get_progress.argtypes = [ctypes.c_void_p]
    lib.ure_session_get_progress.restype = _Progress
    lib.ure_session_get_framebuffer_size.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
    ]
    lib.ure_session_get_framebuffer.argtypes = [ctypes.c_void_p]
    lib.ure_session_get_framebuffer.restype = ctypes.POINTER(ctypes.c_float)
    lib.ure_session_get_aov.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.ure_session_get_aov.restype = ctypes.POINTER(ctypes.c_float)
    lib.ure_session_save_bmp.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.ure_session_save_bmp.restype = ctypes.c_int
    lib.ure_session_save_hdr.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.ure_session_save_hdr.restype = ctypes.c_int
    lib.ure_aov_channel_count.argtypes = [ctypes.c_int]
    lib.ure_aov_channel_count.restype = ctypes.c_int


_native: Optional[ctypes.CDLL] = None


def native() -> ctypes.CDLL:
    global _native
    if _native is None:
        _native = _load_native()
    return _native


def set_min_log_level(level: LogLevel) -> None:
    native().ure_set_min_log_level(int(level))


def _wave_optics_mode_id(mode: str) -> int:
    modes = {
        "radiometric": 0,
        "camera_diffraction": 1,
        "coherent_field": 2,
        "partial_coherence": 3,
    }
    try:
        return modes[mode]
    except KeyError as exc:
        raise ValueError(f"unsupported wave optics mode: {mode}") from exc


def _integrator_mode_id(mode: str) -> int:
    modes = {
        "wavefront": 0,
        "path_guided": 1,
        "restir_di": 2,
        "specular_manifold": 3,
        "mlt": 4,
    }
    try:
        return modes[mode]
    except KeyError as exc:
        raise ValueError(f"unsupported integrator mode: {mode}") from exc


def _integrator_sampler_id(sampler: str) -> int:
    samplers = {
        "default": 0,
        "low_discrepancy": 1,
        "primary_sample_space": 2,
    }
    try:
        return samplers[sampler]
    except KeyError as exc:
        raise ValueError(f"unsupported integrator sampler: {sampler}") from exc


def _integrator_quality_preset_id(preset: str) -> int:
    presets = {
        "default": 0,
        "preview": 1,
        "final": 2,
        "research": 3,
    }
    try:
        return presets[preset]
    except KeyError as exc:
        raise ValueError(f"unsupported integrator quality preset: {preset}") from exc


def _vec3(values: tuple[float, float, float] | list[float]) -> ctypes.Array[ctypes.c_float]:
    if len(values) != 3:
        raise ValueError("expected a 3-float vector")
    return (ctypes.c_float * 3)(float(values[0]), float(values[1]), float(values[2]))


class RenderSession:
    def __init__(
        self,
        num_wavelengths: int = 0,
        queue_capacity: int = 0,
        max_trace_depth: int = 0,
        *,
        domain_bins: int = 0,
        packet_lanes: int = 0,
        max_resident_mb: int = 0,
        wave_optics_mode: str = "radiometric",
        camera_diffraction: bool = False,
        coherent_field: bool = False,
        partial_coherence: bool = False,
        diffractive_materials: bool = False,
        fluorescence: bool = False,
        specular_manifold: bool = False,
        local_fullwave: bool = False,
        allow_wave_preview_degradation: bool = False,
        integrator_mode: str = "wavefront",
        integrator_sampler: str = "default",
        integrator_quality_preset: str = "default",
        allow_biased_integrator_reuse: bool = False,
        path_guiding: bool = False,
        path_guiding_light_mixture: float = 0.5,
        path_guiding_learning_rate: float = 0.25,
        path_guiding_min_weight: float = 1e-6,
        restir_di: bool = False,
        restir_di_max_history: int = 1,
        mlt_chain_count: int = 1,
        mlt_mutations_per_chain: int = 1024,
        mlt_large_step_probability: float = 0.3,
        mlt_small_step_sigma: float = 0.01,
        mlt_seed: int = 1,
    ):
        self._handle: Optional[int] = None
        wave_requested = (
            wave_optics_mode != "radiometric"
            or camera_diffraction
            or coherent_field
            or partial_coherence
            or diffractive_materials
            or fluorescence
            or specular_manifold
            or local_fullwave
            or allow_wave_preview_degradation
        )
        integrator_requested = (
            integrator_mode != "wavefront"
            or integrator_sampler != "default"
            or integrator_quality_preset != "default"
            or allow_biased_integrator_reuse
            or path_guiding
            or restir_di
        )
        if integrator_requested:
            cfg = _SpectralConfig(
                int(domain_bins),
                int(packet_lanes if packet_lanes > 0 else num_wavelengths),
                int(max_resident_mb),
                int(queue_capacity),
                int(max_trace_depth),
            )
            wave = _WaveOpticsConfig(
                _wave_optics_mode_id(wave_optics_mode),
                int(camera_diffraction),
                int(coherent_field),
                int(partial_coherence),
                int(diffractive_materials),
                int(fluorescence),
                int(specular_manifold),
                int(local_fullwave),
                int(allow_wave_preview_degradation),
            )
            integrator = _IntegratorConfig(
                _integrator_mode_id(integrator_mode),
                _integrator_sampler_id(integrator_sampler),
                _integrator_quality_preset_id(integrator_quality_preset),
                int(allow_biased_integrator_reuse),
                int(path_guiding),
                float(path_guiding_light_mixture),
                float(path_guiding_learning_rate),
                float(path_guiding_min_weight),
                int(restir_di),
                1,
                0,
                0,
                int(restir_di_max_history),
                int(specular_manifold),
                2,
                1e-4,
                16,
                int(integrator_mode == "mlt"),
                int(mlt_chain_count),
                int(mlt_mutations_per_chain),
                float(mlt_large_step_probability),
                float(mlt_small_step_sigma),
                int(mlt_seed),
            )
            handle = native().ure_session_create_integrator_config(
                ctypes.byref(cfg),
                ctypes.byref(wave),
                ctypes.byref(integrator),
            )
        elif wave_requested:
            cfg = _SpectralConfig(
                int(domain_bins),
                int(packet_lanes if packet_lanes > 0 else num_wavelengths),
                int(max_resident_mb),
                int(queue_capacity),
                int(max_trace_depth),
            )
            wave = _WaveOpticsConfig(
                _wave_optics_mode_id(wave_optics_mode),
                int(camera_diffraction),
                int(coherent_field),
                int(partial_coherence),
                int(diffractive_materials),
                int(fluorescence),
                int(specular_manifold),
                int(local_fullwave),
                int(allow_wave_preview_degradation),
            )
            handle = native().ure_session_create_wave_config(ctypes.byref(cfg), ctypes.byref(wave))
        elif domain_bins > 0 or packet_lanes > 0 or max_resident_mb > 0:
            cfg = _SpectralConfig(
                int(domain_bins),
                int(packet_lanes if packet_lanes > 0 else num_wavelengths),
                int(max_resident_mb),
                int(queue_capacity),
                int(max_trace_depth),
            )
            handle = native().ure_session_create_spectral_config(ctypes.byref(cfg))
        else:
            handle = native().ure_session_create_config(num_wavelengths, queue_capacity, max_trace_depth)
        if not handle:
            raise RuntimeError("failed to create UltraRender session")
        self._handle = handle

    def close(self) -> None:
        handle = getattr(self, "_handle", None)
        if handle:
            native().ure_session_destroy(handle)
            self._handle = None

    def __enter__(self) -> "RenderSession":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()

    @property
    def handle(self) -> int:
        if not self._handle:
            raise RuntimeError("session is closed")
        return self._handle

    def load_scene_file(self, path: str | os.PathLike[str]) -> None:
        encoded = os.fsencode(path)
        if native().ure_session_load_scene_file(self.handle, encoded) != 0:
            raise RuntimeError(f"failed to load scene: {path}")

    def start(self, progressive: bool = True) -> None:
        if native().ure_session_start(self.handle, 1 if progressive else 0) != 0:
            raise RuntimeError("failed to start render")

    def render_pass(self) -> int:
        spp = native().ure_session_render_pass(self.handle)
        if spp < 0:
            raise RuntimeError("render_pass failed")
        return spp

    def pause(self) -> None:
        native().ure_session_pause(self.handle)

    def resume(self) -> None:
        native().ure_session_resume(self.handle)

    def cancel(self) -> None:
        native().ure_session_cancel(self.handle)

    def reset_accumulation(self) -> None:
        native().ure_session_reset_accumulation(self.handle)

    def update_camera(
        self,
        position: tuple[float, float, float] | list[float],
        look_at: tuple[float, float, float] | list[float],
        fov: float = 45.0,
    ) -> None:
        pos = _vec3(position)
        look = _vec3(look_at)
        if native().ure_session_update_camera(self.handle, pos, look, ctypes.c_float(float(fov))) != 0:
            raise RuntimeError("update_camera failed")

    def update_instance_transform(
        self,
        instance_index: int,
        position: tuple[float, float, float] | list[float],
        scale: tuple[float, float, float] | list[float] = (1.0, 1.0, 1.0),
    ) -> None:
        pos = _vec3(position)
        scl = _vec3(scale)
        if native().ure_session_update_instance_transform(self.handle, instance_index, pos, scl) != 0:
            raise RuntimeError("update_instance_transform failed")

    def update_material(
        self,
        material_index: int,
        material_type: MaterialType = MaterialType.LAMBERTIAN,
        albedo: tuple[float, float, float] | list[float] = (0.8, 0.8, 0.8),
        roughness: float = 0.5,
        ior: float = 1.45,
        emission: tuple[float, float, float] | list[float] = (0.0, 0.0, 0.0),
    ) -> None:
        alb = _vec3(albedo)
        emi = _vec3(emission)
        result = native().ure_session_update_material(
            self.handle,
            material_index,
            int(material_type),
            alb,
            ctypes.c_float(float(roughness)),
            ctypes.c_float(float(ior)),
            emi,
        )
        if result != 0:
            raise RuntimeError("update_material failed")

    def update_material_texture(
        self,
        material_index: int,
        width: int,
        height: int,
        data: list[float] | tuple[float, ...],
        channels: int = 3,
    ) -> None:
        if width <= 0 or height <= 0 or channels < 3:
            raise ValueError("texture dimensions and channels must be positive")
        expected = width * height * channels
        if len(data) != expected:
            raise ValueError(f"texture data length must be {expected}")
        values = (ctypes.c_float * expected)(*(float(v) for v in data))
        result = native().ure_session_update_material_texture(
            self.handle,
            material_index,
            width,
            height,
            channels,
            values,
        )
        if result != 0:
            raise RuntimeError("update_material_texture failed")

    def progress(self) -> Progress:
        raw = native().ure_session_get_progress(self.handle)
        return Progress(raw.spp, SessionState(raw.state), bool(raw.has_scene))

    def framebuffer_size(self) -> tuple[int, int]:
        width = ctypes.c_int()
        height = ctypes.c_int()
        native().ure_session_get_framebuffer_size(self.handle, ctypes.byref(width), ctypes.byref(height))
        return width.value, height.value

    def framebuffer_ptr(self) -> ctypes.POINTER(ctypes.c_float):
        ptr = native().ure_session_get_framebuffer(self.handle)
        if not ptr:
            raise RuntimeError("framebuffer unavailable")
        return ptr

    def framebuffer(self) -> list[float]:
        width, height = self.framebuffer_size()
        if width <= 0 or height <= 0:
            raise RuntimeError("framebuffer size unavailable")
        ptr = self.framebuffer_ptr()
        length = width * height * 3
        return list(ctypes.cast(ptr, ctypes.POINTER(ctypes.c_float * length)).contents)

    def aov_ptr(self, aov: AovType) -> ctypes.POINTER(ctypes.c_float):
        ptr = native().ure_session_get_aov(self.handle, int(aov))
        if not ptr:
            raise RuntimeError(f"AOV unavailable: {aov.name}")
        return ptr

    def aov(self, aov: AovType) -> list[float]:
        width, height = self.framebuffer_size()
        channels = aov_channel_count(aov)
        if width <= 0 or height <= 0 or channels <= 0:
            raise RuntimeError(f"AOV size unavailable: {aov.name}")
        ptr = self.aov_ptr(aov)
        length = width * height * channels
        return list(ctypes.cast(ptr, ctypes.POINTER(ctypes.c_float * length)).contents)

    def save_bmp(self, path: str | os.PathLike[str]) -> None:
        if native().ure_session_save_bmp(self.handle, os.fsencode(path)) != 0:
            raise RuntimeError(f"failed to save BMP: {path}")

    def save_hdr(self, path: str | os.PathLike[str]) -> None:
        if native().ure_session_save_hdr(self.handle, os.fsencode(path)) != 0:
            raise RuntimeError(f"failed to save HDR: {path}")


def create_session(
    num_wavelengths: int = 0,
    queue_capacity: int = 0,
    max_trace_depth: int = 0,
    *,
    domain_bins: int = 0,
    packet_lanes: int = 0,
    max_resident_mb: int = 0,
    wave_optics_mode: str = "radiometric",
    camera_diffraction: bool = False,
    coherent_field: bool = False,
    partial_coherence: bool = False,
    diffractive_materials: bool = False,
    fluorescence: bool = False,
    specular_manifold: bool = False,
    local_fullwave: bool = False,
    allow_wave_preview_degradation: bool = False,
    integrator_mode: str = "wavefront",
    integrator_sampler: str = "default",
    integrator_quality_preset: str = "default",
    allow_biased_integrator_reuse: bool = False,
    path_guiding: bool = False,
    path_guiding_light_mixture: float = 0.5,
    path_guiding_learning_rate: float = 0.25,
    path_guiding_min_weight: float = 1e-6,
    restir_di: bool = False,
    restir_di_max_history: int = 1,
    mlt_chain_count: int = 1,
    mlt_mutations_per_chain: int = 1024,
    mlt_large_step_probability: float = 0.3,
    mlt_small_step_sigma: float = 0.01,
    mlt_seed: int = 1,
) -> RenderSession:
    return RenderSession(
        num_wavelengths,
        queue_capacity,
        max_trace_depth,
        domain_bins=domain_bins,
        packet_lanes=packet_lanes,
        max_resident_mb=max_resident_mb,
        wave_optics_mode=wave_optics_mode,
        camera_diffraction=camera_diffraction,
        coherent_field=coherent_field,
        partial_coherence=partial_coherence,
        diffractive_materials=diffractive_materials,
        fluorescence=fluorescence,
        specular_manifold=specular_manifold,
        local_fullwave=local_fullwave,
        allow_wave_preview_degradation=allow_wave_preview_degradation,
        integrator_mode=integrator_mode,
        integrator_sampler=integrator_sampler,
        integrator_quality_preset=integrator_quality_preset,
        allow_biased_integrator_reuse=allow_biased_integrator_reuse,
        path_guiding=path_guiding,
        path_guiding_light_mixture=path_guiding_light_mixture,
        path_guiding_learning_rate=path_guiding_learning_rate,
        path_guiding_min_weight=path_guiding_min_weight,
        restir_di=restir_di,
        restir_di_max_history=restir_di_max_history,
        mlt_chain_count=mlt_chain_count,
        mlt_mutations_per_chain=mlt_mutations_per_chain,
        mlt_large_step_probability=mlt_large_step_probability,
        mlt_small_step_sigma=mlt_small_step_sigma,
        mlt_seed=mlt_seed,
    )


def aov_channel_count(aov: AovType) -> int:
    return native().ure_aov_channel_count(int(aov))
