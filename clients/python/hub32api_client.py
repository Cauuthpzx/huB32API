"""
hub32api Python client SDK.
Mirrors the Python client in Hub32's WebAPI plugin but for hub32api v1/v2.
"""

import requests
from typing import Optional, List, Dict, Any


class Hub32ApiClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 11081, tls: bool = False):
        scheme = "https" if tls else "http"
        self._base = f"{scheme}://{host}:{port}"
        self._session = requests.Session()
        self._token: Optional[str] = None

    # ------------------------------------------------------------------
    # Authentication
    # ------------------------------------------------------------------
    def authenticate_with_key(self, key_name: str, key_data: str) -> str:
        resp = self._session.post(f"{self._base}/api/v1/auth", json={
            "method": "hub32-key",
            "keyName": key_name,
            "keyData": key_data,
        })
        resp.raise_for_status()
        self._token = resp.json()["token"]
        self._session.headers.update({"Authorization": f"Bearer {self._token}"})
        return self._token

    def logout(self) -> None:
        self._session.delete(f"{self._base}/api/v1/auth")
        self._token = None

    # ------------------------------------------------------------------
    # Computers (v1)
    # ------------------------------------------------------------------
    def list_computers(self, location: str = None, state: str = None) -> List[Dict]:
        params = {}
        if location: params["location"] = location
        if state:    params["state"] = state
        r = self._session.get(f"{self._base}/api/v1/computers", params=params)
        r.raise_for_status()
        return r.json()["computers"]

    def get_computer(self, computer_id: str) -> Dict:
        r = self._session.get(f"{self._base}/api/v1/computers/{computer_id}")
        r.raise_for_status()
        return r.json()

    def get_framebuffer(self, computer_id: str, width: int = 800, height: int = 600,
                        fmt: str = "jpeg", quality: int = 85) -> bytes:
        r = self._session.get(
            f"{self._base}/api/v1/computers/{computer_id}/framebuffer",
            params={"width": width, "height": height, "format": fmt, "quality": quality}
        )
        r.raise_for_status()
        return r.content

    # ------------------------------------------------------------------
    # Features (v1)
    # ------------------------------------------------------------------
    def list_features(self, computer_id: str) -> List[Dict]:
        r = self._session.get(f"{self._base}/api/v1/computers/{computer_id}/features")
        r.raise_for_status()
        return r.json()["features"]

    def start_feature(self, computer_id: str, feature_uid: str, args: Dict = None) -> None:
        self._session.put(
            f"{self._base}/api/v1/computers/{computer_id}/features/{feature_uid}",
            json={"active": True, "arguments": args or {}}
        ).raise_for_status()

    def stop_feature(self, computer_id: str, feature_uid: str, args: Dict = None) -> None:
        self._session.put(
            f"{self._base}/api/v1/computers/{computer_id}/features/{feature_uid}",
            json={"active": False, "arguments": args or {}}
        ).raise_for_status()

    # Convenience wrappers (well-known feature UIDs)
    SCREEN_LOCK_UID = "ccb535a2-1d24-4cc1-a709-8b47d2b2ac79"

    def lock_screen(self, computer_id: str) -> None:
        self.start_feature(computer_id, self.SCREEN_LOCK_UID)

    def unlock_screen(self, computer_id: str) -> None:
        self.stop_feature(computer_id, self.SCREEN_LOCK_UID)

    # ------------------------------------------------------------------
    # Batch operations (v2)
    # ------------------------------------------------------------------
    def batch_feature(self, computer_ids: List[str], feature_uid: str,
                      operation: str = "start", args: Dict = None) -> Dict:
        r = self._session.post(f"{self._base}/api/v2/batch/features", json={
            "computerIds": computer_ids,
            "featureUid": feature_uid,
            "operation": operation,
            "arguments": args or {},
        })
        r.raise_for_status()
        return r.json()

    def list_locations(self) -> List[Dict]:
        r = self._session.get(f"{self._base}/api/v2/locations")
        r.raise_for_status()
        return r.json()["locations"]

    # ------------------------------------------------------------------
    # Session info (v1)
    # ------------------------------------------------------------------
    def get_session(self, computer_id: str) -> Dict:
        r = self._session.get(f"{self._base}/api/v1/computers/{computer_id}/session")
        r.raise_for_status()
        return r.json()

    def get_user(self, computer_id: str) -> Dict:
        r = self._session.get(f"{self._base}/api/v1/computers/{computer_id}/user")
        r.raise_for_status()
        return r.json()

    # ------------------------------------------------------------------
    # Health
    # ------------------------------------------------------------------
    def health(self) -> Dict:
        r = self._session.get(f"{self._base}/api/v2/health")
        r.raise_for_status()
        return r.json()
