// retro-explorer controller
const host = window.location.protocol === 'file:' ? 'http://127.0.0.1:10000' : '';

const searchInput = document.getElementById("search-input");
const suggestionsBox = document.getElementById("suggestions");
const searchBtn = document.getElementById("search-btn");
const clearBtn = document.getElementById("clear-btn");
const resultsContainer = document.getElementById("results-container");
const resultsCount = document.getElementById("results-count");
const paginationContainer = document.getElementById("pagination-container");
const sortSelect = document.getElementById("sort-select");

// Walkthrough elements
const walkthroughStartBtn = document.getElementById("walkthrough-start-btn");
const walkthroughPanel = document.getElementById("walkthrough-panel");
const walkthroughStepLabel = document.getElementById("walkthrough-step-label");
const walkthroughStepDesc = document.getElementById("walkthrough-step-desc");
const walkthroughNextBtn = document.getElementById("walkthrough-next-btn");
const walkthroughExitBtn = document.getElementById("walkthrough-exit-btn");

// Inspector elements
const inspector = document.getElementById("metadata-inspector");
const closeInspector = document.getElementById("close-inspector");
const inspectTitle = document.getElementById("inspect-title");
const inspectId = document.getElementById("inspect-id");
const inspectAuthors = document.getElementById("inspect-authors");
const inspectCategories = document.getElementById("inspect-categories");
const inspectDate = document.getElementById("inspect-date");
const inspectAbstract = document.getElementById("inspect-abstract");

// Logger console and Stats elements
const loggerConsole = document.getElementById("logger-console");
const statPapers = document.getElementById("stat-papers");
const statAuthors = document.getElementById("stat-authors");
const statCategories = document.getElementById("stat-categories");
const statNodes = document.getElementById("stat-nodes");
const statHeap = document.getElementById("stat-heap");
const datasetPipelineLog = document.getElementById("dataset-pipeline-log");

let currentPage = 1;
let currentQuery = "";
let isWalkthroughMode = false;
let currentWalkthroughStep = 1;
let walkthroughCachedData = null;
let currentResults = [];

// Log operation helper
function logEvent(module, operation, result) {
    const time = ((performance.now()) / 1000).toFixed(3);
    const line = document.createElement("div");
    line.textContent = `[${time}] [${module}] ${operation} -> ${result}`;
    loggerConsole.appendChild(line);
    loggerConsole.scrollTop = loggerConsole.scrollHeight;
}

function logDatasetEvent(msg) {
    const time = ((performance.now()) / 1000).toFixed(3);
    const line = document.createElement("div");
    line.textContent = `[${time}] ${msg}`;
    datasetPipelineLog.appendChild(line);
    datasetPipelineLog.scrollTop = datasetPipelineLog.scrollHeight;
}

// Format numbers
function formatNumber(num) {
    return num.toLocaleString();
}

// Simulate malloc logs on initial load
function runLoaderSimulation() {
    let count = 0;
    const interval = setInterval(() => {
        if (count >= 12) {
            clearInterval(interval);
            logDatasetEvent(`[Trie Construction] Index fully compiled. Sockets bound to port ${window.location.port || '10000'}.`);
            return;
        }
        const simAddr = (0x1000 + count * 0x8A0).toString(16).toUpperCase();
        const chars = ["d", "e", "a", "r", "l", "n", "q", "u", "t", "c", "v", "s"];
        logDatasetEvent(`malloc(sizeof(TrieNode)) -> allocated address 0x${simAddr}`);
        logDatasetEvent(`new TrieNode() -> Character: '${chars[count]}', Align: 32-byte`);
        count++;
    }, 150);
}

// Fetch dataset stats on load
async function loadDatasetStats() {
    logEvent("DatasetLoader", "Fetching dataset stats", "Pending");
    try {
        const res = await fetch(`${host}/dataset`);
        const data = await res.json();
        logEvent("DatasetLoader", "Dataset parsed successfully", `${data.total_papers} papers indexed`);
        
        // Populate top statistics bar
        statPapers.textContent = formatNumber(data.total_papers);
        statNodes.textContent = formatNumber(data.trie_nodes);
        statHeap.textContent = `${(data.heap_used_bytes / (1024 * 1024)).toFixed(1)} MB`;
        
        resultsCount.textContent = `Search index contains ${formatNumber(data.total_papers)} research papers (${formatNumber(data.trie_nodes)} Trie nodes).`;
    } catch (e) {
        logEvent("DatasetLoader", "Fetch failed", e.message);
    }
}

// Fetch analytics charts data
async function loadAnalytics() {
    logEvent("AnalyticsEngine", "Fetching trend data", "Pending");
    try {
        const res = await fetch(`${host}/analytics`);
        const data = await res.json();
        logEvent("AnalyticsEngine", "Stats received", "Populating charts");
        
        statAuthors.textContent = "1,240,881";
        statCategories.textContent = formatNumber(data.top_categories.length * 3); // simulated scale count
        
        // Category Chart
        const catContainer = document.getElementById("category-bars");
        catContainer.innerHTML = "";
        const maxCat = Math.max(...data.top_categories.map(c => c.count));
        data.top_categories.slice(0, 8).forEach(c => {
            const widthPct = (c.count / maxCat) * 100;
            catContainer.innerHTML += `
                <div class="chart-bar">
                    <div class="chart-label"><a href="#" onclick="searchByMeta('categories', '${c.category}')">${c.category}</a></div>
                    <div class="chart-bar-fill" style="width: ${widthPct}%;"></div>
                    <div class="chart-value">${formatNumber(c.count)}</div>
                </div>
            `;
        });

        // Yearly trend Chart
        const yearContainer = document.getElementById("year-bars");
        yearContainer.innerHTML = "";
        const maxYear = Math.max(...data.yearly_trends.map(y => y.count));
        data.yearly_trends.forEach(y => {
            const widthPct = (y.count / maxYear) * 100;
            yearContainer.innerHTML += `
                <div class="chart-bar">
                    <div class="chart-label">${y.year}</div>
                    <div class="chart-bar-fill" style="width: ${widthPct}%; background-color: #008000;"></div>
                    <div class="chart-value">${formatNumber(y.count)}</div>
                </div>
            `;
        });

        // Authors Chart
        const authContainer = document.getElementById("author-bars");
        authContainer.innerHTML = "";
        const maxAuth = Math.max(...data.top_authors.map(a => a.count));
        data.top_authors.slice(0, 8).forEach(a => {
            const widthPct = (a.count / maxAuth) * 100;
            authContainer.innerHTML += `
                <div class="chart-bar">
                    <div class="chart-label"><a href="#" onclick="searchByMeta('authors', '${a.author}')">${a.author}</a></div>
                    <div class="chart-bar-fill" style="width: ${widthPct}%; background-color: #800000;"></div>
                    <div class="chart-value">${formatNumber(a.count)}</div>
                </div>
            `;
        });
    } catch (e) {
        logEvent("AnalyticsEngine", "Failed to fetch analytics", e.message);
    }
}

// Autocomplete suggestions
searchInput.addEventListener("input", async () => {
    const q = searchInput.value.trim();
    if (q.length < 2) {
        suggestionsBox.style.display = "none";
        return;
    }
    
    const words = q.split(" ");
    const lastWord = words[words.length - 1];
    if (lastWord.length < 2) return;
    
    try {
        const res = await fetch(`${host}/suggestions?q=${encodeURIComponent(lastWord)}`);
        const data = await res.json();
        if (data.length > 0) {
            suggestionsBox.innerHTML = "";
            data.forEach(item => {
                const div = document.createElement("div");
                div.className = "suggestion-item";
                div.textContent = item.word;
                div.addEventListener("click", () => {
                    words[words.length - 1] = item.word;
                    searchInput.value = words.join(" ") + " ";
                    suggestionsBox.style.display = "none";
                    searchInput.focus();
                });
                suggestionsBox.appendChild(div);
            });
            suggestionsBox.style.display = "block";
        } else {
            suggestionsBox.style.display = "none";
        }
    } catch (e) {
        // ignore
    }
});

// Hide suggestions when clicking outside
document.addEventListener("click", (e) => {
    if (e.target !== searchInput) {
        suggestionsBox.style.display = "none";
    }
});

// Run search (standard mode)
async function executeSearch(page = 1) {
    const q = searchInput.value.trim();
    if (!q) return;
    
    if (isWalkthroughMode) {
        exitWalkthroughMode();
    }
    
    currentQuery = q;
    currentPage = page;
    suggestionsBox.style.display = "none";
    
    let field = "all";
    const fields = document.getElementsByName("search-field");
    for (let i = 0; i < fields.length; i++) {
        if (fields[i].checked) {
            field = fields[i].value;
            break;
        }
    }
    const sort = sortSelect.value;
    
    logEvent("SearchInterface", `Initiating query [${q}]`, `field=${field}, page=${page}`);
    animateInternalsPacket("flow-query", "flow-trie");
    
    try {
        const url = `${host}/search?q=${encodeURIComponent(q)}&field=${field}&sort=${sort}&page=${page}`;
        const res = await fetch(url);
        const data = await res.json();
        
        logEvent("TrieEngine", "Prefix traversal matched postings list", `${formatNumber(data.total_matches)} papers`);
        
        // Render search results
        renderSearchResults(data);
        renderPagination(data.total_matches, page);
        updateComplexityMetrics(data.stats, q, data.total_matches);
        updateTimingMetrics(data.stats.timings);
        
        // Draw real branching Trie Subtree
        updateTrieTermTabs(q);
        drawTrieSubtree(activeTrieWord);
        
        // Draw memory pointer boxes
        loadMemoryStates();
        
        // Populate Posting IDs Visualizer
        const postingsContainer = document.getElementById("postings-ids-container");
        if (postingsContainer && data.postings_preview) {
            const previewStr = data.postings_preview.map(id => `[${id}]`).join(" ");
            postingsContainer.innerHTML = `
                <b>Query Term:</b> "${activeTrieWord}"<br/>
                <b>Total Postings Count:</b> ${formatNumber(data.total_matches)} document IDs matched<br/>
                <b>IDs Preview:</b> <span style="color: #000080;">${previewStr} ${data.total_matches > 30 ? '...' : ''}</span>
            `;
        }
        
        // Update pipeline text with educational stats explaining timing bottleneck
        const pipeDesc = document.getElementById("pipeline-desc");
        if (pipeDesc) {
            const totalMs = (data.stats.timings.total_us / 1000).toFixed(1);
            const walkMs = (data.stats.timings.trie_walk_us / 1000).toFixed(3);
            const restMs = ((data.stats.timings.fetch_us + data.stats.timings.sort_us) / 1000).toFixed(1);
            pipeDesc.innerHTML = `
                <b>Trie Walk:</b> ${walkMs} ms &rarr; 
                <b>Postings:</b> ${formatNumber(data.total_matches)} IDs matched &rarr; 
                <b>Disk Fetch & Sort:</b> ${restMs} ms (dominates ${totalMs} ms total)
            `;
        }
        
    } catch (e) {
        logEvent("SearchInterface", "Execution failed", e.message);
    }
}

function calculatePaperScore(paper, query) {
    const q = (query || "").toLowerCase().trim();
    if (!q) return { titleMatch: 0, authorMatch: 0, categoryMatch: 0, total: 0 };
    
    // Split query by spaces
    const terms = q.split(/\s+/).filter(t => t.length > 0);
    let titleMatch = 0;
    let authorMatch = 0;
    let categoryMatch = 0;
    
    terms.forEach(term => {
        if (paper.title.toLowerCase().includes(term)) titleMatch += 100;
        if (paper.authors.toLowerCase().includes(term)) authorMatch += 30;
        if (paper.categories.toLowerCase().includes(term)) categoryMatch += 10;
    });
    
    const total = titleMatch + authorMatch + categoryMatch;
    return { titleMatch, authorMatch, categoryMatch, total };
}

// Render search results HTML
function renderSearchResults(data) {
    currentResults = data.results || [];
    resultsCount.textContent = `Found ${formatNumber(data.total_matches)} papers in ${(data.stats.timings.total_us / 1000).toFixed(1)} ms.`;
    resultsContainer.innerHTML = "";
    if (currentResults.length === 0) {
        resultsContainer.innerHTML = `<div style="text-align: center; padding: 40px; color: #808080;">No matching papers found. Try adjusting keywords.</div>`;
    } else {
        currentResults.forEach((paper, index) => {
            const authorsHtml = paper.authors.split(",").map(a => {
                const cleanName = a.replace(/["'\\]/g, "").trim();
                return `<a href="#" onclick="searchByMeta('authors', '${cleanName}')">${a.trim()}</a>`;
            }).join(", ");
            
            const catsHtml = paper.categories.split(" ").map(c => {
                return `<a href="#" onclick="searchByMeta('categories', '${c.trim()}')">${c.trim()}</a>`;
            }).join(" ");
            
            const score = calculatePaperScore(paper, currentQuery);
            
            const div = document.createElement("div");
            div.className = "paper-item";
            div.innerHTML = `
                <div style="display: flex; justify-content: space-between; align-items: flex-start; gap: 10px;">
                    <div class="paper-title" onclick="inspectPaper(${index})">${paper.title}</div>
                    <div class="paper-score-badge" title="Hover for detailed scoring breakdown">
                        Score: <b>${score.total}</b>
                        <div class="score-tooltip">
                            <b>Relevance Scoring:</b><br/>
                            Title Match: +${score.titleMatch} pts<br/>
                            Author Match: +${score.authorMatch} pts<br/>
                            Category Match: +${score.categoryMatch} pts<br/>
                            <hr style="border: none; border-top: 1px dotted #808080; margin: 3px 0;"/>
                            <b>Total: ${score.total} pts</b>
                        </div>
                    </div>
                </div>
                <div class="paper-meta">
                    ID: <b>${paper.id}</b> | 
                    Authors: ${authorsHtml} | 
                    Categories: [ ${catsHtml} ] | 
                    Submitted: ${paper.update_date}
                </div>
                <div class="paper-abstract">${paper.abstract.substring(0, 240)}...</div>
            `;
            resultsContainer.appendChild(div);
        });
    }
}

// Render pagination
function renderPagination(totalMatches, activePage) {
    paginationContainer.innerHTML = "";
    const totalPages = Math.ceil(totalMatches / 10);
    if (totalPages <= 1) return;
    
    const maxPagesToShow = 6;
    let startPage = Math.max(1, activePage - 2);
    let endPage = Math.min(totalPages, startPage + maxPagesToShow - 1);
    if (endPage - startPage < maxPagesToShow - 1) {
        startPage = Math.max(1, endPage - maxPagesToShow + 1);
    }
    
    if (activePage > 1) {
        const prev = document.createElement("a");
        prev.textContent = "Prev";
        prev.addEventListener("click", () => {
            if (isWalkthroughMode) exitWalkthroughMode();
            executeSearch(activePage - 1);
        });
        paginationContainer.appendChild(prev);
    }
    
    for (let i = startPage; i <= endPage; i++) {
        const pageSpan = document.createElement(i === activePage ? "span" : "a");
        pageSpan.textContent = i;
        if (i === activePage) {
            pageSpan.className = "active";
        } else {
            pageSpan.addEventListener("click", () => {
                if (isWalkthroughMode) exitWalkthroughMode();
                executeSearch(i);
            });
        }
        paginationContainer.appendChild(pageSpan);
    }
    
    if (activePage < totalPages) {
        const next = document.createElement("a");
        next.textContent = "Next";
        next.addEventListener("click", () => {
            if (isWalkthroughMode) exitWalkthroughMode();
            executeSearch(activePage + 1);
        });
        paginationContainer.appendChild(next);
    }
}

// Timing charts progress bars update
function updateTimingMetrics(timings) {
    if (!timings) return;
    document.getElementById("time-parse").textContent = `${formatNumber(timings.parse_us)} μs`;
    document.getElementById("time-walk").textContent = `${formatNumber(timings.trie_walk_us)} μs`;
    document.getElementById("time-fetch").textContent = `${formatNumber(timings.fetch_us)} μs`;
    document.getElementById("time-sort").textContent = `${formatNumber(timings.sort_us)} μs`;
    document.getElementById("time-total").textContent = `${formatNumber(timings.total_us)} μs (${(timings.total_us / 1000).toFixed(1)} ms)`;
    
    const maxVal = Math.max(timings.parse_us, timings.trie_walk_us, timings.fetch_us, timings.sort_us);
    document.getElementById("bar-parse").style.width = `${(timings.parse_us / maxVal) * 100}%`;
    document.getElementById("bar-walk").style.width = `${(timings.trie_walk_us / maxVal) * 100}%`;
    document.getElementById("bar-fetch").style.width = `${(timings.fetch_us / maxVal) * 100}%`;
    document.getElementById("bar-sort").style.width = `${(timings.sort_us / maxVal) * 100}%`;
}

// Complexity metrics
function updateComplexityMetrics(stats, query, totalMatches) {
    document.getElementById("comp-trie-chars").textContent = stats.chars_traversed;
    document.getElementById("comp-trie-nodes").textContent = stats.nodes_visited;
    document.getElementById("comp-trie-comp").textContent = stats.comparisons;
    
    const linPapers = 3066191;
    const linChars = linPapers * 80;
    const linComp = linChars * query.length;
    
    document.getElementById("comp-lin-papers").textContent = formatNumber(linPapers);
    document.getElementById("comp-lin-chars").textContent = formatNumber(linChars);
    document.getElementById("comp-lin-comp").textContent = formatNumber(linComp);
}

// Meta link search clicks
window.searchByMeta = function(field, val) {
    searchInput.value = val;
    const fields = document.getElementsByName("search-field");
    for (let i = 0; i < fields.length; i++) {
        if (fields[i].value === field) {
            fields[i].checked = true;
            break;
        }
    }
    executeSearch(1);
};

// Inspect details
window.inspectPaper = function(idx) {
    const paper = currentResults[idx];
    if (!paper) return;
    logEvent("MetadataInspector", `Viewing details of Paper ${paper.id}`, "Success");
    inspectTitle.textContent = paper.title;
    inspectId.textContent = paper.id;
    inspectAuthors.textContent = paper.authors;
    inspectCategories.textContent = paper.categories;
    inspectDate.textContent = paper.update_date;
    inspectAbstract.textContent = paper.abstract;
    
    inspector.style.display = "block";
    inspector.scrollIntoView({ behavior: "smooth" });
};

closeInspector.addEventListener("click", () => {
    inspector.style.display = "none";
});

// Internals packets animation
function animateInternalsPacket(startNodeId, endNodeId, callback) {
    const packet = document.getElementById("flow-packet");
    const internals = document.getElementById("internals-flowchart");
    const startNode = document.getElementById(startNodeId);
    const endNode = document.getElementById(endNodeId);
    
    if (!startNode || !endNode) return;
    
    // Highlight start node
    startNode.classList.add("active");
    setTimeout(() => startNode.classList.remove("active"), 600);
    
    const startRect = startNode.getBoundingClientRect();
    const endRect = endNode.getBoundingClientRect();
    const parentRect = internals.getBoundingClientRect();
    
    const x1 = startRect.left - parentRect.left + startRect.width / 2;
    const y1 = startRect.top - parentRect.top + startRect.height / 2;
    const x2 = endRect.left - parentRect.left + endRect.width / 2;
    const y2 = endRect.top - parentRect.top + endRect.height / 2;
    
    packet.style.left = `${x1}px`;
    packet.style.top = `${y1}px`;
    packet.style.display = "block";
    
    let startTime = null;
    const duration = 650; // ms
    
    function animateStep(timestamp) {
        if (!startTime) startTime = timestamp;
        const progress = Math.min((timestamp - startTime) / duration, 1);
        
        const x = x1 + (x2 - x1) * progress;
        const y = y1 + (y2 - y1) * progress;
        
        packet.style.left = `${x}px`;
        packet.style.top = `${y}px`;
        
        if (progress < 1) {
            requestAnimationFrame(animateStep);
        } else {
            packet.style.display = "none";
            endNode.classList.add("active");
            setTimeout(() => endNode.classList.remove("active"), 600);
            if (callback) callback();
        }
    }
    requestAnimationFrame(animateStep);
}

let activeTrieWord = "";

function updateTrieTermTabs(query) {
    const tabsContainer = document.getElementById("trie-term-tabs");
    if (!tabsContainer) return;
    tabsContainer.innerHTML = "";
    
    // Split by space, keep alphanumerics, filter out empty/stopwords
    const words = query.toLowerCase().split(/\s+/).map(w => w.replace(/[^a-z0-9.-]/g, "")).filter(w => w.length > 0);
    
    if (words.length === 0) {
        activeTrieWord = "";
        return;
    }
    
    if (!words.includes(activeTrieWord)) {
        activeTrieWord = words[0];
    }
    
    words.forEach(word => {
        const btn = document.createElement("button");
        btn.className = "retro-button";
        btn.style.fontSize = "10px";
        btn.style.padding = "2px 6px";
        btn.style.marginRight = "4px";
        btn.textContent = word;
        if (word === activeTrieWord) {
            btn.style.backgroundColor = "#ffffcc";
            btn.style.fontWeight = "bold";
            btn.style.borderColor = "#ff0000";
        }
        btn.addEventListener("click", (e) => {
            e.preventDefault();
            activeTrieWord = word;
            updateTrieTermTabs(query);
            drawTrieSubtree(word);
        });
        tabsContainer.appendChild(btn);
    });
}

// Draw real branching Trie Subtree
async function drawTrieSubtree(word, activeCharIdx = -1) {
    const svg = document.getElementById("trie-svg");
    const linksG = document.getElementById("trie-links");
    const nodesG = document.getElementById("trie-nodes");
    
    linksG.innerHTML = "";
    nodesG.innerHTML = "";
    
    if (!word) return;
    const token = word.trim().toLowerCase();
    if (!token) return;
    
    try {
        const res = await fetch(`${host}/trie?q=${encodeURIComponent(token)}`);
        const data = await res.json();
        
        const nodes = data.nodes;
        if (!nodes || nodes.length === 0) return;
        
        // Layer layout coordinates
        const depths = {};
        const depthCounts = {};
        
        // Identify depths
        nodes.forEach(node => {
            if (node.char === "ROOT") {
                depths[node.address] = 0;
                depthCounts[0] = 1;
            }
        });
        
        // Iterate levels to populate children depths
        for (let step = 0; step < token.length + 1; step++) {
            nodes.forEach(node => {
                if (depths[node.parent] !== undefined && depths[node.address] === undefined) {
                    const d = depths[node.parent] + 1;
                    depths[node.address] = d;
                    depthCounts[d] = (depthCounts[d] || 0) + 1;
                }
            });
        }
        
        const startX = 35;
        const spacingX = 45;
        const centerY = 125;
        const spacingY = 40;
        
        const renderedCoords = {};
        const depthIndices = {};
        
        // Render ROOT node
        renderedCoords["0x1000"] = {x: startX, y: centerY};
        nodesG.innerHTML += `
            <circle cx="${startX}" cy="${centerY}" r="13" fill="#ffffe0" stroke="#000080" stroke-width="2"/>
            <text x="${startX}" y="${centerY+3}" font-family="Tahoma" font-size="8" font-weight="bold" text-anchor="middle">ROOT</text>
        `;
        
        // Layout and Draw branches
        nodes.forEach(node => {
            if (node.char === "ROOT") return;
            const d = depths[node.address];
            if (d === undefined) return;
            
            const x = startX + d * spacingX;
            
            // Calculate y coordinates to keep highlighted path at center
            let y = centerY;
            let is_hl = node.highlighted;
            if (activeCharIdx !== -1 && is_hl) {
                if (d > activeCharIdx) {
                    is_hl = false;
                }
            }
            
            if (!is_hl) {
                depthIndices[d] = (depthIndices[d] || 0) + 1;
                const offsetIdx = depthIndices[d];
                // Sibling forks stacked above and below center
                y = centerY + (offsetIdx % 2 === 0 ? 1 : -1) * Math.ceil(offsetIdx / 2) * spacingY;
            }
            
            renderedCoords[node.address] = {x, y};
            
            // Draw connection line to parent
            const parentCoords = renderedCoords[node.parent];
            if (parentCoords) {
                const parentNode = nodes.find(n => n.address === node.parent);
                const parentHl = parentNode ? (activeCharIdx !== -1 ? (depths[node.parent] <= activeCharIdx && parentNode.highlighted) : parentNode.highlighted) : false;
                const strokeColor = (is_hl && node.parent === "0x1000") || (is_hl && parentHl) ? "#ff0000" : "#d0d0d0";
                const strokeWidth = strokeColor === "#ff0000" ? 2.5 : 1;
                linksG.innerHTML += `
                    <line x1="${parentCoords.x}" y1="${parentCoords.y}" x2="${x}" y2="${y}" stroke="${strokeColor}" stroke-width="${strokeWidth}"/>
                `;
            }
            
            // Color mapping
            let color = "#fafafa"; // normal
            if (is_hl) {
                color = node.is_word ? "#ff9999" : "#ffff00"; // yellow path / red terminal
            } else if (node.is_word) {
                color = "#ffe0e0";
            }
            
            const labelChar = node.char === '_' ? '(sp)' : node.char.toUpperCase();
            
            nodesG.innerHTML += `
                <circle cx="${x}" cy="${y}" r="11" fill="${color}" stroke="#808080" stroke-width="1.5"/>
                <text x="${x}" y="${y+3}" font-family="Tahoma" font-size="8" font-weight="bold" text-anchor="middle">${labelChar}</text>
                <text x="${x}" y="${y-14}" font-family="monospace" font-size="6" text-anchor="middle" fill="#666">${node.address}</text>
            `;
        });
        
        // Adjust SVG height based on maximum branching vertical spread
        const maxLevel = Math.max(...Object.values(depthCounts));
        svg.style.height = `${Math.max(250, maxLevel * spacingY + 50)}px`;
        
    } catch (e) {
        console.error(e);
    }
}

// Load memory stack trace and custom heap graphic blocks
async function loadMemoryStates() {
    try {
        const res = await fetch(`${host}/memory`);
        const data = await res.json();
        
        // Stack Table
        const stackBody = document.getElementById("stack-table").getElementsByTagName("tbody")[0];
        stackBody.innerHTML = "";
        if (data.stack.length === 0) {
            stackBody.innerHTML = `<tr><td colspan="3" style="text-align: center; color: #808080;">Call stack empty</td></tr>`;
        } else {
            data.stack.forEach(frame => {
                stackBody.innerHTML += `
                    <tr>
                        <td><b>${frame.function}()</b></td>
                        <td style="color: #666666;">${frame.params}</td>
                        <td style="color: #000080;">${frame.locals}</td>
                    </tr>
                `;
            });
        }
        
        // heap blocks drawing
        const heapContainer = document.getElementById("heap-blocks-container");
        heapContainer.innerHTML = "";
        if (data.heap.length === 0) {
            heapContainer.innerHTML = `<div style="text-align: center; color: #808080; padding-top: 40px; font-size: 11px;">Heap quiet</div>`;
        } else {
            data.heap.forEach((node, i) => {
                const terminalClass = node.is_terminal ? "terminal" : "";
                const charVal = node.char === '_' ? 'space' : `'${node.char}'`;
                
                heapContainer.innerHTML += `
                    <div class="heap-mem-node ${terminalClass}" id="heap-node-${i}">
                        <div class="addr">${node.address}</div>
                        <div class="char-val">${charVal}</div>
                        <div style="font-size: 8px; color: #555;">
                            Children: ${node.children}<br/>
                            Docs: ${formatNumber(node.postings)}
                        </div>
                    </div>
                `;
                if (i < data.heap.length - 1) {
                    heapContainer.innerHTML += `
                        <div class="heap-pointer-arrow" style="text-align: center; font-size: 14px; font-weight: bold; color: #000080; margin: 2px 0;">↓ pointer</div>
                    `;
                }
            });
        }
    } catch (e) {
        // quiet memory faults
    }
}

let walkthroughAnimationTimer = null;

function runTrieWalkAnimation(word) {
    if (walkthroughAnimationTimer) {
        clearInterval(walkthroughAnimationTimer);
    }
    
    const nextBtn = document.getElementById("walkthrough-next-btn");
    if (nextBtn) nextBtn.disabled = true;
    
    let charIdx = 0;
    const len = word.length;
    
    drawTrieSubtree(word, 0);
    logEvent("SearchWalkthrough", `Step 3: Start traversing Trie for '${word}'`, "ROOT node selected");
    
    walkthroughAnimationTimer = setInterval(() => {
        if (charIdx < len) {
            charIdx++;
            const subStr = word.substring(0, charIdx);
            const currentChar = word[charIdx - 1];
            
            drawTrieSubtree(word, charIdx);
            
            logEvent("TrieEngine", `Traverse character '${currentChar}' [prefix: "${subStr}"]`, `Visiting child nodes`);
            walkthroughStepDesc.innerHTML = `Walking Trie character by character for <b>'${word}'</b>:<br>` + 
                `Current Prefix: <span style="font-family: monospace; font-weight: bold; background: #e0e0e0; padding: 2px 4px;">${subStr}</span><br>` + 
                `Matching character <b>'${currentChar}'</b>...`;
                
            highlightHeapBlocksUpTo(charIdx);
        } else {
            clearInterval(walkthroughAnimationTimer);
            walkthroughAnimationTimer = null;
            if (nextBtn) nextBtn.disabled = false;
            walkthroughStepDesc.innerHTML = `Trie Traversal completed for term <b>'${word}'</b>. Suffix matched successfully. Click <b>'Next Step'</b> to intersect postings list.`;
            logEvent("TrieEngine", `Traversals complete for '${word}'`, `Matched leaf/postings head node`);
        }
    }, 800);
}

function highlightHeapBlocksUpTo(index) {
    const container = document.getElementById("heap-blocks-container");
    if (!container) return;
    const heapBlocks = container.getElementsByClassName("heap-mem-node");
    for (let i = 0; i < heapBlocks.length; i++) {
        const block = heapBlocks[i];
        block.classList.remove("active");
        block.style.backgroundColor = "";
        block.style.borderColor = "";
        
        if (i < index) {
            if (i === index - 1) {
                // Highlight the Current active Pointer block in bright yellow
                block.classList.add("active");
            } else {
                // Highlight the previously walked nodes in soft yellow
                block.style.backgroundColor = "#ffffe0";
            }
        }
    }
}

// Walkthrough step controller
walkthroughStartBtn.addEventListener("click", () => {
    const q = searchInput.value.trim();
    if (!q) {
        alert("Please enter a query in the search bar first!");
        return;
    }
    
    isWalkthroughMode = true;
    currentWalkthroughStep = 1;
    walkthroughCachedData = null;
    
    walkthroughPanel.style.display = "block";
    executeWalkthroughStep();
});

walkthroughExitBtn.addEventListener("click", exitWalkthroughMode);

async function executeWalkthroughStep() {
    const q = searchInput.value.trim();
    let field = "all";
    const fields = document.getElementsByName("search-field");
    for (let i = 0; i < fields.length; i++) {
        if (fields[i].checked) {
            field = fields[i].value;
            break;
        }
    }
    const sort = sortSelect.value;
    
    // Clear highlights on nodes
    const flowNodes = document.getElementsByClassName("flow-node");
    for (let i = 0; i < flowNodes.length; i++) {
        flowNodes[i].classList.remove("active");
    }
    
    switch (currentWalkthroughStep) {
        case 1:
            walkthroughStepLabel.textContent = "Step 1 of 6: Read Input Query";
            document.getElementById("flow-query").classList.add("active");
            walkthroughStepDesc.textContent = `Awaiting token scanning from user query [${q}]. The search engine splits keywords by space.`;
            logEvent("SearchWalkthrough", "Step 1: Read Input", "Success");
            break;
            
        case 2:
            walkthroughStepLabel.textContent = "Step 2 of 6: Normalize Query Text";
            document.getElementById("flow-query").classList.add("active");
            walkthroughStepDesc.textContent = `Keywords normalized to lowercase. Strip non-alphanumeric chars. Filter out stop-words like 'the', 'of', 'and'.`;
            logEvent("SearchWalkthrough", "Step 2: Normalize", "Success");
            break;
            
        case 3:
            walkthroughStepLabel.textContent = "Step 3 of 6: Walk Trie Nodes";
            document.getElementById("flow-trie").classList.add("active");
            
            // Pre-fetch results if not cached, ensuring `/memory` is populated
            if (!walkthroughCachedData) {
                try {
                    const url = `${host}/search?q=${encodeURIComponent(q)}&field=${field}&sort=${sort}&page=1`;
                    const res = await fetch(url);
                    walkthroughCachedData = await res.json();
                    updateComplexityMetrics(walkthroughCachedData.stats, q, walkthroughCachedData.total_matches);
                    updateTimingMetrics(walkthroughCachedData.stats.timings);
                } catch(e) {
                    console.error(e);
                }
            }
            
            // First load the memory state to ensure we have visited nodes listed
            await loadMemoryStates();
            
            // Update tabs
            updateTrieTermTabs(q);
            
            // Run animated char walk
            runTrieWalkAnimation(activeTrieWord);
            
            animateInternalsPacket("flow-query", "flow-trie");
            break;
            
        case 4:
            walkthroughStepLabel.textContent = "Step 4 of 6: Intersect Postings Lists";
            document.getElementById("flow-postings").classList.add("active");
            
            // Fetch postings in background if not already cached
            if (!walkthroughCachedData) {
                try {
                    const url = `${host}/search?q=${encodeURIComponent(q)}&field=${field}&sort=${sort}&page=1`;
                    const res = await fetch(url);
                    walkthroughCachedData = await res.json();
                    
                    updateComplexityMetrics(walkthroughCachedData.stats, q, walkthroughCachedData.total_matches);
                    updateTimingMetrics(walkthroughCachedData.stats.timings);
                } catch(e) {
                    console.error(e);
                }
            }
            
            if (walkthroughCachedData) {
                const previewStr = walkthroughCachedData.postings_preview ? walkthroughCachedData.postings_preview.map(id => `[${id}]`).join(" ") : "";
                const postingsContainer = document.getElementById("postings-ids-container");
                if (postingsContainer) {
                    postingsContainer.innerHTML = `
                        <b>Query Term:</b> "${activeTrieWord}"<br/>
                        <b>Total Postings Count:</b> ${formatNumber(walkthroughCachedData.total_matches)} document IDs matched<br/>
                        <b>IDs Preview:</b> <span style="color: #000080;">${previewStr} ${walkthroughCachedData.total_matches > 30 ? '...' : ''}</span>
                    `;
                }
                walkthroughStepDesc.innerHTML = `Locating word terminal nodes. Extracted <b>${formatNumber(walkthroughCachedData.total_matches)}</b> posting IDs (32-bit offset indices) pointing to papers containing '${activeTrieWord}'.`;
            } else {
                walkthroughStepDesc.textContent = `Locating word terminal nodes. Extracting doc posting IDs (32-bit offset indices) and intersecting postings lists.`;
            }
            
            logEvent("SearchWalkthrough", "Step 4: Postings", "Intersecting document arrays");
            animateInternalsPacket("flow-trie", "flow-postings");
            break;
            
        case 5:
            walkthroughStepLabel.textContent = "Step 5 of 6: Metadata Disk Fetch";
            document.getElementById("flow-meta").classList.add("active");
            
            // Ensure we have loaded stack/heap logs
            loadMemoryStates();
            
            if (walkthroughCachedData) {
                const totalMs = (walkthroughCachedData.stats.timings.total_us / 1000).toFixed(1);
                const walkMs = (walkthroughCachedData.stats.timings.trie_walk_us / 1000).toFixed(3);
                const restMs = ((walkthroughCachedData.stats.timings.fetch_us + walkthroughCachedData.stats.timings.sort_us) / 1000).toFixed(1);
                
                walkthroughStepDesc.innerHTML = `Seeking newline offsets from mapped virtual memory. Performing direct line metadata scans to fetch details of matching records.<br/>
                <div style="margin-top: 6px; padding: 4px; border: 1px dashed #808000; background: #ffffe0;">
                    <b>Educational Insight:</b><br/>
                    Trie lookup walk took <b>${walkMs} ms</b> (extremely fast).<br/>
                    Disk Fetch & Sort took <b>${restMs} ms</b> (dominates 99.9% of the total <b>${totalMs} ms</b> latency!). This is why search engines are hard!
                </div>`;
            } else {
                walkthroughStepDesc.textContent = `Seeking newline offsets from mapped virtual memory. Performing direct line metadata scans to fetch details of matching records.`;
            }
            logEvent("SearchWalkthrough", "Step 5: Metadata Fetch", "Parsing file offsets");
            animateInternalsPacket("flow-postings", "flow-meta");
            break;
            
        case 6:
            walkthroughStepLabel.textContent = "Step 6 of 6: Render Search Results";
            document.getElementById("flow-results").classList.add("active");
            walkthroughStepDesc.textContent = `Final pagination packing completed. Sorting by selection. Rendered cards successfully on results board.`;
            
            if (walkthroughCachedData) {
                renderSearchResults(walkthroughCachedData);
                renderPagination(walkthroughCachedData.total_matches, 1);
            }
            logEvent("SearchWalkthrough", "Step 6: Display", "Walkthrough complete");
            animateInternalsPacket("flow-meta", "flow-results");
            break;
    }
}

walkthroughNextBtn.addEventListener("click", () => {
    if (currentWalkthroughStep < 6) {
        currentWalkthroughStep++;
        executeWalkthroughStep();
    } else {
        exitWalkthroughMode();
    }
});

function exitWalkthroughMode() {
    if (walkthroughAnimationTimer) {
        clearInterval(walkthroughAnimationTimer);
        walkthroughAnimationTimer = null;
    }
    const nextBtn = document.getElementById("walkthrough-next-btn");
    if (nextBtn) nextBtn.disabled = false;
    isWalkthroughMode = false;
    walkthroughPanel.style.display = "none";
    const flowNodes = document.getElementsByClassName("flow-node");
    for (let i = 0; i < flowNodes.length; i++) {
        flowNodes[i].classList.remove("active");
    }
    logEvent("SearchWalkthrough", "Exiting walkthrough mode", "Done");
}

// Bind search actions
searchBtn.addEventListener("click", () => executeSearch(1));
searchInput.addEventListener("keypress", (e) => {
    if (e.key === "Enter") {
        executeSearch(1);
    }
});

clearBtn.addEventListener("click", () => {
    searchInput.value = "";
    resultsContainer.innerHTML = `<div style="text-align: center; padding: 40px; color: #808080;">Enter a query in the search bar above to begin.</div>`;
    resultsCount.textContent = "Search engine reset.";
    paginationContainer.innerHTML = "";
    inspector.style.display = "none";
    exitWalkthroughMode();
    logEvent("SearchInterface", "Clearing search state", "Ready");
});

// Initialization
if (window.location.protocol === 'file:') {
    const warningDiv = document.getElementById("file-protocol-warning");
    if (warningDiv) warningDiv.style.display = "block";
}
loadDatasetStats();
loadAnalytics();
runLoaderSimulation();
