// pdf_logic.js (Shared logic for both overlays)
const pdfs = [
{ name: "Sample PDF 1", url : "https://www.w3.org/WAI/ER/tests/xhtml/testfiles/resources/pdf/dummy.pdf" },
{ name: "Sample PDF 2", url : "https://www.orimi.com/pdf-test.pdf" },
{ name: "Sample PDF 3", url : "https://www.africau.edu/images/default/sample.pdf" }
];

let selectedIndex = 0;
let isViewerOpen = false;
let currentPage = 1;

const pdfList = document.getElementById('pdfList');
const pdfViewer = document.getElementById('pdfViewer');
const status = document.getElementById('status');

function renderList() {
    pdfList.innerHTML = '';
    pdfs.forEach((pdf, index) = > {
        const li = document.createElement('li');
        li.textContent = pdf.name;
        if (index == = selectedIndex) li.classList.add('selected');
        pdfList.appendChild(li);
    });
}

function updateStatus(message) {
    status.textContent = message;
}

function openPDF() {
    if (isViewerOpen) return;
    const pdf = pdfs[selectedIndex];
    pdfViewer.src = pdf.url;
    pdfViewer.style.display = 'block';
    pdfList.style.display = 'none';
    isViewerOpen = true;
    currentPage = 1;
    updateStatus(`Viewing ${ pdf.name } | Page ${ currentPage } | Left / Right to navigate, Esc to close`);
}

function closePDF() {
    if (!isViewerOpen) return;
    pdfViewer.src = '';
    pdfViewer.style.display = 'none';
    pdfList.style.display = 'block';
    isViewerOpen = false;
    updateStatus('Use arrow keys to select, Enter to open, Esc to close, Left/Right to navigate');
}

function navigatePDF(direction) {
    if (!isViewerOpen) return;
    currentPage += direction;
    if (currentPage < 1) currentPage = 1;
    updateStatus(`Viewing ${ pdfs[selectedIndex].name } | Page ${ currentPage } | Left / Right to navigate, Esc to close`);
}

document.addEventListener('keydown', (e) = > {
    if (!isViewerOpen) {
        if (e.key == = 'ArrowDown' && selectedIndex < pdfs.length - 1) {
            selectedIndex++;
            renderList();
        }
        else if (e.key == = 'ArrowUp' && selectedIndex > 0) {
            selectedIndex--;
            renderList();
        }
        else if (e.key == = 'Enter') {
            openPDF();
        }
        else if (e.key == = 'Escape') {
            window.close();
        }
    }
    else {
        if (e.key == = 'Escape') {
            closePDF();
        }
        else if (e.key == = 'ArrowLeft') {
            navigatePDF(-1);
        }
        else if (e.key == = 'ArrowRight') {
            navigatePDF(1);
        }
    }
});

updateStatus('Use arrow keys to select, Enter to open, Esc to close, Left/Right to navigate');
renderList();