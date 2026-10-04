import esphome.codegen as cg
from esphome.types import ConfigType
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    async def to_code_testing(config: ConfigType) -> None:
        # nut_server.cpp sizes its client table from this define
        cg.add_define("NUT_SERVER_MAX_CLIENTS", 2)

    manifest.to_code = to_code_testing
