static void resetToDefaults(RawEngine* e) {
    e->setExposure(0.0f); e->setContrast(1.0f); e->setHighlights(0.0f); e->setShadows(0.0f);
    e->setWhites(0.0f); e->setBlacks(0.0f); e->setVibrance(0.0f); e->setSaturation(0.0f);
    e->setTemperature(0.0f); e->setTint(0.0f); e->setTonemappingEnabled(false);
    e->setGrainAmount(0.0f); e->setGrainSize(1.0f); e->setGrainRoughness(0.5f);
    e->setVignetteAmount(0.0f); e->setVignetteMidpoint(50.0f); e->setVignetteRoundness(0.0f); e->setVignetteFeather(50.0f);
    
    e->setHslRedHue(0.0f); e->setHslRedSaturation(0.0f); e->setHslRedLuminance(0.0f);
    e->setHslOrangeHue(0.0f); e->setHslOrangeSaturation(0.0f); e->setHslOrangeLuminance(0.0f);
    e->setHslYellowHue(0.0f); e->setHslYellowSaturation(0.0f); e->setHslYellowLuminance(0.0f);
    e->setHslGreenHue(0.0f); e->setHslGreenSaturation(0.0f); e->setHslGreenLuminance(0.0f);
    e->setHslAquaHue(0.0f); e->setHslAquaSaturation(0.0f); e->setHslAquaLuminance(0.0f);
    e->setHslBlueHue(0.0f); e->setHslBlueSaturation(0.0f); e->setHslBlueLuminance(0.0f);
    e->setHslPurpleHue(0.0f); e->setHslPurpleSaturation(0.0f); e->setHslPurpleLuminance(0.0f);
    e->setHslMagentaHue(0.0f); e->setHslMagentaSaturation(0.0f); e->setHslMagentaLuminance(0.0f);

    e->setCgShadowsHue(0.0f); e->setCgShadowsSaturation(0.0f); e->setCgShadowsLuminance(0.0f);
    e->setCgMidtonesHue(0.0f); e->setCgMidtonesSaturation(0.0f); e->setCgMidtonesLuminance(0.0f);
    e->setCgHighlightsHue(0.0f); e->setCgHighlightsSaturation(0.0f); e->setCgHighlightsLuminance(0.0f);
    e->setCgBalance(0.0f); e->setCgBlending(50.0f);
}

void RawEngine::loadEdits() {
  if (m_source.isEmpty()) return;

  QFileInfo fileInfo(m_source);
  QString editsPath = fileInfo.absolutePath() + "/.PhotonData/edits/" + fileInfo.fileName() + ".json";

  m_editStack.clear();

  if (!QFile::exists(editsPath)) {
    resetToDefaults(this);
    m_editIndex = -1;
    commitEdit(); // This will create the initial state and set index to 0
    return;
  }

  QFile file(editsPath);
  if (!file.open(QIODevice::ReadOnly)) return;

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  QJsonArray arr = doc.array();
  if (arr.isEmpty()) {
      resetToDefaults(this);
      m_editIndex = -1;
      commitEdit();
      return;
  }

  for (const auto& val : arr) {
      m_editStack.append(val.toObject().toVariantMap());
  }
  m_editIndex = m_editStack.size() - 1;
  
  // Apply last state - this will trigger signals and update UI
  applyJsonToState(this, arr.last().toObject());

  emit editStackChanged();
  emit canUndoChanged();
  emit canRedoChanged();
}

void RawEngine::commitEdit() {
  if (m_source.isEmpty()) return;

  QJsonObject newState = stateToJson(this);
  
  // If we were in the middle of undo history, truncate the future
  if (m_editIndex < (int)m_editStack.size() - 1) {
      while (m_editStack.size() > m_editIndex + 1) {
          m_editStack.removeLast();
      }
  }

  // Check if it's actually different from the last one in the stack
  if (!m_editStack.isEmpty()) {
      QJsonObject lastState = QJsonObject::fromVariantMap(m_editStack.last().toMap());
      if (newState == lastState) return;
  }

  m_editStack.append(newState.toVariantMap());
  m_editIndex = m_editStack.size() - 1;

  emit editStackChanged();
  emit canUndoChanged();
  emit canRedoChanged();

  // Save full stack to file
  QFileInfo fileInfo(m_source);
  QString editsDir = fileInfo.absolutePath() + "/.PhotonData/edits";
  QDir().mkpath(editsDir);
  QString editsPath = editsDir + "/" + fileInfo.fileName() + ".json";

  QJsonArray arr;
  for (const auto& v : m_editStack) {
      arr.append(QJsonObject::fromVariantMap(v.toMap()));
  }

  QFile file(editsPath);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(arr).toJson());
  }
}

void RawEngine::undo() {
    if (!canUndo()) return;
    m_editIndex--;
    applyJsonToState(this, QJsonObject::fromVariantMap(m_editStack[m_editIndex].toMap()));
    emit canUndoChanged();
    emit canRedoChanged();
}

void RawEngine::redo() {
    if (!canRedo()) return;
    m_editIndex++;
    applyJsonToState(this, QJsonObject::fromVariantMap(m_editStack[m_editIndex].toMap()));
    emit canUndoChanged();
    emit canRedoChanged();
}
