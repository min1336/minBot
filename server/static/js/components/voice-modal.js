/**
 * Voice learning modal — 3-step workflow: provider → upload → test.
 */

import { switchVoiceProvider, uploadVoiceSamples } from '../api.js';
import { showToast } from '../app.js';

export function initVoiceModal() {
  // Step 1: Provider selection
  const radios = document.querySelectorAll('input[name="voice-provider"]');
  radios.forEach(radio => {
    radio.addEventListener('change', async () => {
      try {
        await switchVoiceProvider(radio.value);
        showToast(`프로바이더 변경: ${radio.value}`, 'success');
      } catch (err) {
        showToast(err.message, 'error');
      }
    });
  });

  // Step 2: File upload
  const dropzone = document.getElementById('voice-dropzone');
  const fileInput = document.getElementById('voice-file-input');
  const fileList = document.getElementById('voice-file-list');
  const uploadBtn = document.getElementById('btn-upload-voice');
  let selectedFiles = [];

  dropzone.addEventListener('click', () => fileInput.click());

  dropzone.addEventListener('dragover', (e) => {
    e.preventDefault();
    dropzone.classList.add('dragover');
  });

  dropzone.addEventListener('dragleave', () => {
    dropzone.classList.remove('dragover');
  });

  dropzone.addEventListener('drop', (e) => {
    e.preventDefault();
    dropzone.classList.remove('dragover');
    addFiles(e.dataTransfer.files);
  });

  fileInput.addEventListener('change', () => {
    addFiles(fileInput.files);
    fileInput.value = '';
  });

  function addFiles(files) {
    for (const f of files) {
      selectedFiles.push(f);
    }
    renderFileList();
  }

  function renderFileList() {
    while (fileList.firstChild) fileList.removeChild(fileList.firstChild);

    selectedFiles.forEach((f, i) => {
      const item = document.createElement('div');
      item.className = 'file-list-item';

      const nameSpan = document.createElement('span');
      nameSpan.textContent = f.name;

      const sizeSpan = document.createElement('span');
      sizeSpan.className = 'text-muted';
      sizeSpan.textContent = ` (${(f.size / 1024).toFixed(1)} KB)`;
      nameSpan.appendChild(sizeSpan);

      const removeBtn = document.createElement('button');
      removeBtn.className = 'btn btn-sm btn-ghost';
      removeBtn.textContent = '\u00d7';
      removeBtn.addEventListener('click', () => {
        selectedFiles.splice(i, 1);
        renderFileList();
      });

      item.appendChild(nameSpan);
      item.appendChild(removeBtn);
      fileList.appendChild(item);
    });
    uploadBtn.disabled = selectedFiles.length === 0;
  }

  uploadBtn.addEventListener('click', async () => {
    if (selectedFiles.length === 0) return;
    uploadBtn.disabled = true;
    uploadBtn.textContent = '업로드 중...';
    try {
      const result = await uploadVoiceSamples(selectedFiles);
      showToast(`${result.uploaded}개 파일 업로드 완료 (${result.total_duration}초)`, 'success');
      selectedFiles = [];
      renderFileList();
    } catch (err) {
      showToast(err.message, 'error');
    } finally {
      uploadBtn.textContent = '업로드';
      uploadBtn.disabled = selectedFiles.length === 0;
    }
  });

  // Step 3: Test synthesis (placeholder)
  const testBtn = document.getElementById('btn-test-voice');
  testBtn.addEventListener('click', () => {
    showToast('합성 테스트는 향후 확장 예정입니다', 'info');
  });
}
