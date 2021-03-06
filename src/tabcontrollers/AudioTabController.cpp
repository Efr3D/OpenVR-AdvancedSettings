#include "AudioTabController.h"
#include <QQuickWindow>
#include <QApplication>
#include "../overlaycontroller.h"
#include "audiomanager/AudioManagerWindows.h"

// application namespace
namespace advsettings {

	void AudioTabController::initStage1() {
		vr::EVRSettingsError vrSettingsError;
		audioManager.reset(new AudioManagerWindows());
		audioManager->init(this);
		m_playbackDevices = audioManager->getPlaybackDevices();
		m_recordingDevices = audioManager->getRecordingDevices();
		findPlaybackDeviceIndex(audioManager->getPlaybackDevId(), false);
		char deviceId[1024];
		vr::VRSettings()->GetString(vr::k_pch_audio_Section, vr::k_pch_audio_OnPlaybackMirrorDevice_String, deviceId, 1024, &vrSettingsError);
		if (vrSettingsError != vr::VRSettingsError_None) {
			LOG(WARNING) << "Could not read \"" << vr::k_pch_audio_OnPlaybackMirrorDevice_String << "\" setting: " << vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);
		} else {
			audioManager->setMirrorDevice(deviceId);
			findMirrorDeviceIndex(audioManager->getMirrorDevId(), false);
			lastMirrorDevId = deviceId;
			m_mirrorVolume = audioManager->getMirrorVolume();
			m_mirrorMuted = audioManager->getMirrorMuted();
		}
		findMicDeviceIndex(audioManager->getMicDevId(), false);
		m_micVolume = audioManager->getMicVolume();
		m_micMuted = audioManager->getMicMuted();
		reloadPttProfiles();
		reloadPttConfig();
		eventLoopTick();
	}


	void AudioTabController::initStage2(OverlayController * parent, QQuickWindow * widget) {
		this->parent = parent;
		this->widget = widget;

		std::string notifKey = std::string(OverlayController::applicationKey) + ".pptnotification";
		vr::VROverlayError overlayError = vr::VROverlay()->CreateOverlay(notifKey.c_str(), notifKey.c_str(), &m_ulNotificationOverlayHandle);
		if (overlayError == vr::VROverlayError_None) {
			std::string notifIconPath = QApplication::applicationDirPath().toStdString() + "/res/qml/ptt_notification.png";
			if (QFile::exists(QString::fromStdString(notifIconPath))) {
				vr::VROverlay()->SetOverlayFromFile(m_ulNotificationOverlayHandle, notifIconPath.c_str());
				vr::VROverlay()->SetOverlayWidthInMeters(m_ulNotificationOverlayHandle, 0.02f);
				vr::HmdMatrix34_t notificationTransform = {
					1.0f, 0.0f, 0.0f, 0.12f,
					0.0f, 1.0f, 0.0f, 0.08f,
					0.0f, 0.0f, 1.0f, -0.3f
				};
				vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(m_ulNotificationOverlayHandle, vr::k_unTrackedDeviceIndex_Hmd, &notificationTransform);
			} else {
				LOG(ERROR) << "Could not find notification icon \"" << notifIconPath << "\"";
			}
		} else {
			LOG(ERROR) << "Could not create ptt notification overlay: " << vr::VROverlay()->GetOverlayErrorNameFromEnum(overlayError);
		}
	}


	float AudioTabController::mirrorVolume() const {
		return m_mirrorVolume;
	}


	bool AudioTabController::mirrorMuted() const {
		return m_mirrorMuted;
	}


	float AudioTabController::micVolume() const {
		return m_micVolume;
	}


	bool AudioTabController::micMuted() const {
		return m_micMuted;
	}

	void AudioTabController::eventLoopTick() {
		if (!eventLoopMutex.try_lock()) {
			return;
		}
		if (settingsUpdateCounter >= 50) {
			vr::EVRSettingsError vrSettingsError;
			char mirrorDeviceId[1024];
			vr::VRSettings()->GetString(vr::k_pch_audio_Section, vr::k_pch_audio_OnPlaybackMirrorDevice_String, mirrorDeviceId, 1024, &vrSettingsError);
			if (vrSettingsError != vr::VRSettingsError_None) {
				LOG(WARNING) << "Could not read \"" << vr::k_pch_audio_OnPlaybackMirrorDevice_String << "\" setting: " << vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);
			}
			if (lastMirrorDevId.compare(mirrorDeviceId) != 0) {
				audioManager->setMirrorDevice(mirrorDeviceId);
				findMirrorDeviceIndex(audioManager->getMirrorDevId());
				lastMirrorDevId = mirrorDeviceId;
			}
			if (m_mirrorDeviceIndex >= 0) {
				setMirrorVolume(audioManager->getMirrorVolume());
				setMirrorMuted(audioManager->getMirrorMuted());
			}
			if (m_recordingDeviceIndex >= 0) {
				setMicVolume(audioManager->getMicVolume());
				setMicMuted(audioManager->getMicMuted());
			}
			settingsUpdateCounter = 0;
		} else {
			settingsUpdateCounter++;
		}
		checkPttStatus();
		eventLoopMutex.unlock();
	}

	bool AudioTabController::pttChangeValid() {
		return audioManager && audioManager->isMicValid();
	}

	void AudioTabController::onPttStart() {
		setMicMuted(false);
	}

	void AudioTabController::onPttEnabled() {
		setMicMuted(true);
	}

	void AudioTabController::onPttStop() {
		setMicMuted(true);
	}

	void AudioTabController::onPttDisabled() {
		setMicMuted(false);
	}

	void AudioTabController::setMirrorVolume(float value, bool notify) {
		std::lock_guard<std::recursive_mutex> lock(eventLoopMutex);
		if (value != m_mirrorVolume) {
			m_mirrorVolume = value;
			if (audioManager->isMirrorValid()) {
				audioManager->setMirrorVolume(value);
			}
			if (notify) {
				emit mirrorVolumeChanged(value);
			}
		}
	}

	void AudioTabController::setMirrorMuted(bool value, bool notify) {
		std::lock_guard<std::recursive_mutex> lock(eventLoopMutex);
		if (value != m_mirrorMuted) {
			m_mirrorMuted = value;
			if (audioManager->isMirrorValid()) {
				audioManager->setMirrorMuted(value);
			}
			if (notify) {
				emit mirrorMutedChanged(value);
			}
		}
	}

	void AudioTabController::setMicVolume(float value, bool notify) {
		std::lock_guard<std::recursive_mutex> lock(eventLoopMutex);
		if (value != m_micVolume) {
			m_micVolume = value;
			if (audioManager->isMicValid()) {
				audioManager->setMicVolume(value);
			}
			if (notify) {
				emit micVolumeChanged(value);
			}
		}
	}

	void AudioTabController::setMicMuted(bool value, bool notify) {
		std::lock_guard<std::recursive_mutex> lock(eventLoopMutex);
		if (value != m_micMuted) {
			m_micMuted = value;
			if (audioManager->isMicValid()) {
				audioManager->setMicMuted(value);
			}
			if (notify) {
				emit micMutedChanged(value);
			}
		}
	}

	void AudioTabController::onNewRecordingDevice() {
		findMicDeviceIndex(audioManager->getMicDevId());
	}

	void AudioTabController::onNewPlaybackDevice() {
		findPlaybackDeviceIndex(audioManager->getPlaybackDevId());
	}

	void AudioTabController::onNewMirrorDevice() {
	}

	void AudioTabController::onDeviceAdded() {
	}

	void AudioTabController::onDeviceRemoved() {
	}

	int AudioTabController::getPlaybackDeviceCount() {
		return (int)m_playbackDevices.size();
	}

	QString AudioTabController::getPlaybackDeviceName(int index) {
		if (index < m_playbackDevices.size()) {
			return QString::fromStdString(m_playbackDevices[index].second);
		} else {
			return "<ERROR>";
		}
	}

	int AudioTabController::getRecordingDeviceCount() {
		return (int)m_recordingDevices.size();
	}

	QString AudioTabController::getRecordingDeviceName(int index) {
		if (index < m_recordingDevices.size()) {
			return QString::fromStdString(m_recordingDevices[index].second);
		} else {
			return "<ERROR>";
		}
	}


	int AudioTabController::playbackDeviceIndex() const {
		return m_playbackDeviceIndex;
	}

	int AudioTabController::mirrorDeviceIndex() const {
		return m_mirrorDeviceIndex;
	}

	int AudioTabController::micDeviceIndex() const {
		return m_recordingDeviceIndex;
	}

	void AudioTabController::setPlaybackDeviceIndex(int index, bool notify) {
		if (index != m_playbackDeviceIndex) {
			if (index < m_playbackDevices.size() && index != m_mirrorDeviceIndex) {
				vr::EVRSettingsError vrSettingsError;
				vr::VRSettings()->SetString(vr::k_pch_audio_Section, vr::k_pch_audio_OnPlaybackDevice_String, m_playbackDevices[index].first.c_str(), &vrSettingsError);
				if (vrSettingsError != vr::VRSettingsError_None) {
					LOG(WARNING) << "Could not write \"" << vr::k_pch_audio_OnPlaybackDevice_String << "\" setting: " << vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);
				} else {
					vr::VRSettings()->Sync();
					audioManager->setPlaybackDevice(m_playbackDevices[index].first, notify);
				}
			} else if (notify) {
				emit playbackDeviceIndexChanged(m_playbackDeviceIndex);
			}
		}
	}

	void AudioTabController::setMirrorDeviceIndex(int index, bool notify) {
		if (index != m_mirrorDeviceIndex) {
			if (index == -1) {
				vr::EVRSettingsError vrSettingsError;
				vr::VRSettings()->RemoveKeyInSection(vr::k_pch_audio_Section, vr::k_pch_audio_OnPlaybackMirrorDevice_String, &vrSettingsError);
				if (vrSettingsError != vr::VRSettingsError_None) {
					LOG(WARNING) << "Could not remove \"" << vr::k_pch_audio_OnPlaybackMirrorDevice_String << "\" setting: " << vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);
				} else {
					vr::VRSettings()->Sync();
					audioManager->setMirrorDevice("", notify);
				}
			} else if (index < m_playbackDevices.size() && index != m_playbackDeviceIndex && index != m_mirrorDeviceIndex) {
				vr::EVRSettingsError vrSettingsError;
				vr::VRSettings()->SetString(vr::k_pch_audio_Section, vr::k_pch_audio_OnPlaybackMirrorDevice_String, m_playbackDevices[index].first.c_str(), &vrSettingsError);
				if (vrSettingsError != vr::VRSettingsError_None) {
					LOG(WARNING) << "Could not write \"" << vr::k_pch_audio_OnPlaybackMirrorDevice_String << "\" setting: " << vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);
				} else {
					vr::VRSettings()->Sync();
					audioManager->setMirrorDevice(m_playbackDevices[index].first, notify);
				}
			} else if (notify) {
				emit mirrorDeviceIndexChanged(index);
			}
		}
	}

	void AudioTabController::setMicDeviceIndex(int index, bool notify) {
		if (index != m_recordingDeviceIndex) {
			if (index < m_recordingDevices.size()) {
				vr::EVRSettingsError vrSettingsError;
				vr::VRSettings()->SetString(vr::k_pch_audio_Section, vr::k_pch_audio_OnRecordDevice_String, m_recordingDevices[index].first.c_str(), &vrSettingsError);
				if (vrSettingsError != vr::VRSettingsError_None) {
					LOG(WARNING) << "Could not write \"" << vr::k_pch_audio_OnRecordDevice_String << "\" setting: " << vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);
				} else {
					vr::VRSettings()->Sync();
					audioManager->setMicDevice(m_recordingDevices[index].first, notify);
				}
			} else if (notify) {
				emit micDeviceIndexChanged(index);
			}
		}
	}

	void AudioTabController::findPlaybackDeviceIndex(std::string id, bool notify) {
		int i = 0;
		bool deviceFound = false;
		for (auto d : m_playbackDevices) {
			if (d.first.compare(id) == 0) {
				deviceFound = true;
				break;
			} else {
				++i;
			}
		}
		if (deviceFound) {
			m_playbackDeviceIndex = i;
			if (notify) {
				emit playbackDeviceIndexChanged(i);
			}
		}
	}

	void AudioTabController::findMirrorDeviceIndex(std::string id, bool notify) {
		int i = 0;
		bool deviceFound = false;
		for (auto d : m_playbackDevices) {
			if (d.first.compare(id) == 0) {
				deviceFound = true;
				break;
			} else {
				++i;
			}
		}
		if (deviceFound && m_mirrorDeviceIndex != i) {
			m_mirrorDeviceIndex = i;
			if (notify) {
				emit mirrorDeviceIndexChanged(i);
			}
		}
	}

	void AudioTabController::findMicDeviceIndex(std::string id, bool notify) {
		int i = 0;
		bool deviceFound = false;
		for (auto d : m_recordingDevices) {
			if (d.first.compare(id) == 0) {
				deviceFound = true;
				break;
			} else {
				++i;
			}
		}
		if (deviceFound) {
			m_recordingDeviceIndex = i;
			if (notify) {
				emit micDeviceIndexChanged(i);
			}
		}
	}

} // namespace advconfig
