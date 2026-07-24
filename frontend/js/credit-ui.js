/* credit-ui.js — 界面渲染（DOM）：信用档案 / 纠纷中心 / 星级评价 / 纠纷弹窗 / 大厅标识 */
(function (global) {
    'use strict';

    const API = global.CreditAPI;
    if (!API) { console.error('CreditAPI not loaded'); return; }

    let creditDirection = 'received';
    let creditMapCache = null;
    let creditMapTs = 0;
    let pendingRatingOrder = null;
    let pendingDisputeOrder = null;
    let selectedStars = 0;
    let currentDetailDisputeId = null;

    const el = {};

    function $(id) { return document.getElementById(id); }

    function toast(msg) {
        const t = $('toast');
        if (!t) { alert(msg); return; }
        t.textContent = msg;
        t.classList.remove('hidden');
        clearTimeout(toast._t);
        toast._t = setTimeout(() => t.classList.add('hidden'), 3000);
    }

    function esc(s) {
        if (s == null) return '';
        return String(s)
            .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
    }

    function starsHtml(score) {
        const full = Math.round(score || 0);
        let html = '';
        for (let i = 1; i <= 5; i++) {
            html += '<i class="fa' + (i <= full ? 's' : 'r') + ' fa-star' + (i <= full ? '' : '-empty') + '"></i>';
        }
        return html;
    }

    async function loadCreditMap(force) {
        const now = Date.now();
        if (!force && creditMapCache && now - creditMapTs < 30000) return creditMapCache;
        try {
            creditMapCache = await API.getCreditsMap();
            creditMapTs = now;
        } catch (e) { creditMapCache = creditMapCache || {}; }
        return creditMapCache;
    }

    /* ---------- Credit Profile Tab ---------- */

    async function loadCreditTab() {
        const u = API.currentUser();
        if (!u) return;
        el.creditName.textContent = u.realName || u.username;
        try {
            const info = await API.getCredit(u.username);
            renderCreditHero(info);
            const updated = Object.assign({}, u, {
                creditScore: info.creditScore, grade: info.grade, canAccept: info.canAccept
            });
            API.saveUser(updated);
            const statCredit = $('stat-credit');
            if (statCredit) statCredit.textContent = info.creditScore;
        } catch (e) {
            el.creditScore.textContent = '--';
            el.creditStatus.textContent = '加载失败：' + e.message;
        }
        await loadRatingList();
    }

    function renderCreditHero(info) {
        el.creditScore.textContent = info.creditScore;
        const pct = Math.max(0, Math.min(100, info.creditScore));
        el.creditRing.style.setProperty('--p', pct);
        el.creditRing.className = 'credit-ring grade-' + info.grade;
        el.creditGrade.textContent = info.grade;
        el.creditGrade.className = 'credit-grade-badge grade-' + info.grade;
        el.givenCount.textContent = info.givenCount;
        el.receivedCount.textContent = info.receivedCount;
        if (info.canAccept) {
            el.creditStatus.innerHTML = '<i class="fas fa-circle-check" style="color:#10b981"></i> 信用良好，可正常接单与发布';
        } else {
            el.creditStatus.innerHTML = '<i class="fas fa-ban" style="color:#ef4444"></i> 信用分低于 ' + info.minAccept + '，已暂停接单资格（发布不受影响）';
        }
    }

    async function loadRatingList() {
        const u = API.currentUser();
        if (!u) return;
        el.ratingList.innerHTML = '<div class="rating-empty">加载中...</div>';
        try {
            const list = await API.getRatings(u.username, creditDirection);
            renderRatingList(list);
        } catch (e) {
            el.ratingList.innerHTML = '<div class="rating-empty">加载失败：' + esc(e.message) + '</div>';
        }
    }

    function renderRatingList(list) {
        if (!list || list.length === 0) {
            el.ratingList.innerHTML = '<div class="rating-empty">暂无' + (creditDirection === 'given' ? '给出' : '收到') + '的评价</div>';
            return;
        }
        el.ratingList.innerHTML = list.map(r => {
            const who = creditDirection === 'received'
                ? API.maskName(r.fromDisplay || r.fromUser)
                : (r.toDisplay || r.toUser);
            const label = creditDirection === 'received' ? '评价了我' : '我评价了';
            const initial = (who || '?').charAt(0);
            return '<div class="rating-item">'
                + '<div class="rating-avatar">' + esc(initial) + '</div>'
                + '<div class="rating-body">'
                +   '<div class="rating-top">'
                +     '<div class="rating-who">' + esc(who) + '<span class="muted">' + label + '</span></div>'
                +     '<div class="rating-stars">' + starsHtml(r.score) + '</div>'
                +   '</div>'
                +   (r.comment ? '<div class="rating-comment">' + esc(r.comment) + '</div>' : '')
                +   '<div class="rating-meta">'
                +     '<span><i class="fas fa-hashtag"></i> 订单 #' + r.orderId + '</span>'
                +     '<span><i class="far fa-clock"></i> ' + esc(r.time || API.formatTime(r.createdAt)) + '</span>'
                +   '</div>'
                + '</div></div>';
        }).join('');
    }

    /* ---------- Dispute Center Tab ---------- */

    async function loadDisputesTab() {
        const u = API.currentUser();
        el.disputeList.innerHTML = '<div class="rating-empty">加载中...</div>';
        try {
            const list = await API.getDisputes(u ? u.username : '');
            renderDisputeList(list);
        } catch (e) {
            el.disputeList.innerHTML = '<div class="rating-empty">加载失败：' + esc(e.message) + '</div>';
        }
    }

    function renderDisputeList(list) {
        if (!list || list.length === 0) {
            el.disputeList.innerHTML = '<div class="rating-empty">暂无纠纷单。若订单存在争议，可在「我的订单」对应卡片上发起纠纷。</div>';
            return;
        }
        el.disputeList.innerHTML = list.map(d => {
            const statusCls = d.status;
            const statusTxt = API.disputeStatusLabel(d.status);
            const isPending = d.status === 'pending';
            return '<div class="dispute-card status-' + esc(statusCls) + '" data-id="' + d.id + '">'
                + '<div class="dispute-head">'
                +   '<h4><i class="fas fa-scale-balanced"></i> 纠纷 #' + d.id + ' · 订单 #' + d.orderId + '</h4>'
                +   '<span class="dispute-status-tag ' + esc(statusCls) + '">' + esc(statusTxt) + '</span>'
                + '</div>'
                + '<div class="dispute-info">'
                +   '<div><i class="fas fa-box"></i> <strong>包裹：</strong>' + esc(d.package || '-') + '</div>'
                +   '<div><i class="fas fa-coins"></i> <strong>悬赏：</strong>' + esc(d.reward || '-') + '</div>'
                +   '<div><i class="fas fa-user-pen"></i> <strong>发起人：</strong>' + esc(API.maskName(d.initiator)) + '</div>'
                +   '<div><i class="far fa-clock"></i> <strong>发起时间：</strong>' + esc(d.createdTime || API.formatTime(d.createdAt)) + '</div>'
                + '</div>'
                + '<div class="dispute-reason-text"><strong>纠纷原因：</strong>' + esc(d.reason) + '</div>'
                + (d.resolvedTime ? '<div class="rating-meta" style="margin-top:8px;"><i class="fas fa-flag-checkered"></i> 裁决时间：' + esc(d.resolvedTime) + '</div>' : '')
                + (d.result ? '<div class="dispute-result-text ' + esc(statusCls) + '"><strong>裁决结果：</strong>' + esc(d.result) + '</div>' : '')
                + '<div class="dispute-actions">'
                +   '<button class="btn-outline" data-act="view" style="flex:0 0 auto;padding:8px 18px;">查看详情</button>'
                +   (isPending ? '<button class="btn-primary" data-act="respond" style="flex:0 0 auto;padding:8px 18px;background:var(--accent);">补充说明</button>' : '')
                + '</div>'
                + '</div>';
        }).join('');

        el.disputeList.querySelectorAll('.dispute-card').forEach(card => {
            const id = parseInt(card.dataset.id, 10);
            card.querySelector('[data-act="view"]').onclick = () => openDisputeDetail(id);
            const respBtn = card.querySelector('[data-act="respond"]');
            if (respBtn) respBtn.onclick = () => openDisputeDetail(id, true);
        });
    }

    async function openDisputeDetail(id, focusRespond) {
        try {
            const d = await API.getDispute(id);
            currentDetailDisputeId = id;
            const u = API.currentUser();
            const isCreator = u && d.creator === u.username;
            const isWorker = u && d.worker === u.username;
            const isPending = d.status === 'pending';
            const canRespond = isPending && (isCreator || isWorker);
            const alreadyResponded = (isCreator && d.creatorResp) || (isWorker && d.workerResp);

            let html = '<div class="dispute-info" style="margin-top:14px;">'
                + '<div><i class="fas fa-hashtag"></i> <strong>纠纷编号：</strong>#' + d.id + '</div>'
                + '<div><i class="fas fa-box"></i> <strong>关联订单：</strong>#' + d.orderId + ' · ' + esc(d.package || '') + '</div>'
                + '<div><i class="fas fa-user-pen"></i> <strong>发布方：</strong>' + esc(API.maskName(d.creator)) + '</div>'
                + '<div><i class="fas fa-person-running"></i> <strong>接单方：</strong>' + esc(API.maskName(d.worker)) + '</div>'
                + '<div><i class="fas fa-coins"></i> <strong>悬赏：</strong>' + esc(d.reward || '-') + '</div>'
                + '<div><i class="far fa-clock"></i> <strong>发起时间：</strong>' + esc(d.createdTime || API.formatTime(d.createdAt)) + '</div>'
                + '</div>'
                + '<div class="dispute-reason-text" style="margin-top:12px;"><strong>纠纷原因（发起人：' + esc(API.maskName(d.initiator)) + '）：</strong><br>' + esc(d.reason) + '</div>';

            if (d.creatorResp) html += '<div class="resp-row creator"><span class="resp-label">发布方说明：</span>' + esc(d.creatorResp) + '</div>';
            if (d.workerResp) html += '<div class="resp-row worker"><span class="resp-label">接单方说明：</span>' + esc(d.workerResp) + '</div>';

            if (d.resolvedTime) {
                html += '<div class="rating-meta" style="margin-top:10px;"><i class="fas fa-flag-checkered"></i> 裁决时间：' + esc(d.resolvedTime) + '</div>';
            }
            if (d.result) {
                html += '<div class="dispute-result-text ' + esc(d.status) + '"><strong>裁决结果：</strong>' + esc(d.result) + '</div>';
            } else if (isPending) {
                html += '<div class="dispute-warn" style="margin-top:12px;">纠纷审理中：双方可补充说明；若 24 小时内双方均未补充，系统将按接单方责任自动裁决并退款。</div>';
            }

            $('dispute-detail-body').innerHTML = html;

            const block = $('dispute-respond-block');
            const respondInput = $('dispute-respond-text');
            if (canRespond && !alreadyResponded) {
                block.classList.remove('hidden');
                respondInput.value = '';
                respondInput.placeholder = isCreator ? '以发布方身份提供证据或说明（提交后不可更改）' : '以接单方身份提供证据或说明（提交后不可更改）';
            } else {
                block.classList.add('hidden');
            }

            $('dispute-detail-modal').classList.remove('hidden');
            if (focusRespond && canRespond && !alreadyResponded) {
                setTimeout(() => respondInput.focus(), 100);
            }
        } catch (e) {
            toast('加载纠纷详情失败：' + e.message);
        }
    }

    async function submitDisputeResponse() {
        if (!currentDetailDisputeId) return;
        const u = API.currentUser();
        const text = $('dispute-respond-text').value.trim();
        if (!text) { toast('请填写补充说明'); return; }
        try {
            await API.respondDispute(currentDetailDisputeId, u.username, text);
            toast('补充说明已提交');
            $('dispute-detail-modal').classList.add('hidden');
            loadDisputesTab();
        } catch (e) { toast(e.message); }
    }

    /* ---------- Rating Modal ---------- */

    function openRatingModal(order) {
        pendingRatingOrder = order;
        selectedStars = 0;
        $('rating-target-desc').textContent = '订单 #' + order.id + ' · 评价对象：'
            + API.maskName(API.ratingTarget(order, API.currentUser().username));
        $('rating-comment').value = '';
        updateStars(0);
        $('rating-modal').classList.remove('hidden');
    }

    function updateStars(n) {
        el.stars.forEach(s => {
            const v = parseInt(s.dataset.v, 10);
            const icon = s.querySelector('i');
            if (v <= n) { icon.className = 'fas fa-star'; s.classList.add('active'); }
            else { icon.className = 'far fa-star'; s.classList.remove('active'); }
        });
    }

    async function submitRating() {
        if (!pendingRatingOrder) return;
        if (selectedStars < 1) { toast('请选择星级'); return; }
        const u = API.currentUser();
        const comment = $('rating-comment').value.trim();
        try {
            const res = await API.submitRating(pendingRatingOrder.id, u.username, selectedStars, comment);
            toast('评价成功！对方信用分已更新至 ' + res.newScore + '（' + res.grade + '）');
            $('rating-modal').classList.add('hidden');
            pendingRatingOrder = null;
            if (global.App && global.App.refreshCurrentView) global.App.refreshCurrentView();
            if (global.App && global.App.refreshProfile) global.App.refreshProfile();
        } catch (e) { toast(e.message); }
    }

    /* ---------- Dispute Modal ---------- */

    function openDisputeModal(order) {
        pendingDisputeOrder = order;
        $('dispute-target-desc').textContent = '订单 #' + order.id + ' · ' + (order.package || '');
        $('dispute-reason').value = '';
        $('dispute-modal').classList.remove('hidden');
    }

    async function submitDispute() {
        if (!pendingDisputeOrder) return;
        const reason = $('dispute-reason').value.trim();
        if (!reason) { toast('请填写纠纷原因'); return; }
        const u = API.currentUser();
        try {
            await API.createDispute(pendingDisputeOrder.id, u.username, reason);
            toast('纠纷已发起，订单已冻结');
            $('dispute-modal').classList.add('hidden');
            pendingDisputeOrder = null;
            if (global.App && global.App.refreshCurrentView) global.App.refreshCurrentView();
        } catch (e) { toast(e.message); }
    }

    /* ---------- Card decoration (called from script.js) ---------- */

    function decorateHallCard(cardEl, order) {
        if (!cardEl || !order) return;
        const creatorRow = cardEl.querySelector('.info-row i.fa-user-circle');
        if (creatorRow && creatorRow.parentElement) {
            const badge = document.createElement('span');
            badge.className = 'creator-credit grade-' + (order.creatorGrade || API.gradeOf(order.creatorScore || 100));
            badge.innerHTML = '<i class="fas fa-shield-halved"></i> ' + esc(order.creatorGrade || '中')
                + ' ' + (order.creatorScore != null ? order.creatorScore : '');
            creatorRow.parentElement.appendChild(badge);
        }
    }

    function addLowCreditBanner(container, orders) {
        if (!container) return;
        const hasLow = (orders || []).some(o => o.status === 'pending' && o.creatorLow);
        const existing = container.querySelector('.low-credit-banner');
        if (hasLow && !existing) {
            const b = document.createElement('div');
            b.className = 'low-credit-banner';
            b.innerHTML = '<i class="fas fa-triangle-exclamation"></i> 列表顶部存在「低信用发布」任务，请谨慎接单。';
            container.insertBefore(b, container.firstChild);
        } else if (!hasLow && existing) {
            existing.remove();
        }
    }

    async function decorateMyOrderFooter(cardEl, order) {
        if (!cardEl || !order) return;
        const footer = cardEl.querySelector('.order-footer');
        if (!footer) return;
        const u = API.currentUser();
        if (!u) return;

        if (order.status === 'completed') {
            const already = await hasMyRating(order.id, u.username);
            if (!already) {
                const btn = document.createElement('button');
                btn.className = 'btn-rate';
                btn.innerHTML = '<i class="fas fa-star"></i> 评价对方';
                btn.onclick = () => openRatingModal(order);
                footer.appendChild(btn);
            }
        }
        if (API.canFileDispute(order, u.username)) {
            const btn = document.createElement('button');
            btn.className = 'btn-danger-outline';
            btn.innerHTML = '<i class="fas fa-gavel"></i> 发起纠纷';
            btn.onclick = () => openDisputeModal(order);
            footer.appendChild(btn);
        }
        if (order.status === 'disputed') {
            const tag = document.createElement('button');
            tag.className = 'btn-outline';
            tag.innerHTML = '<i class="fas fa-scale-balanced"></i> 查看纠纷';
            tag.onclick = async () => {
                const all = await API.getDisputes(u.username);
                const d = all.find(x => x.orderId === order.id);
                if (d) openDisputeDetail(d.id);
            };
            footer.appendChild(tag);
        }
    }

    let myRatingsCache = null;
    let myRatingsCacheTs = 0;
    async function hasMyRating(orderId, username) {
        const now = Date.now();
        if (!myRatingsCache || now - myRatingsCacheTs > 15000) {
            try {
                myRatingsCache = await API.getRatings(username, 'given');
                myRatingsCacheTs = now;
            } catch (e) { myRatingsCache = []; }
        }
        return (myRatingsCache || []).some(r => r.orderId === orderId && r.fromUser === username);
    }

    function invalidateCaches() {
        myRatingsCache = null;
        creditMapCache = null;
    }

    /* ---------- Init ---------- */

    function bindNav() {
        document.querySelectorAll('.nav-item').forEach(item => {
            item.addEventListener('click', () => {
                const tab = item.dataset.tab;
                if (tab === 'credit') { invalidateCaches(); loadCreditTab(); }
                else if (tab === 'disputes') { invalidateCaches(); loadDisputesTab(); }
            });
        });
        const gotoCredit = $('goto-credit-btn');
        if (gotoCredit) gotoCredit.onclick = () => {
            const nav = document.querySelector('[data-tab="credit"]');
            if (nav) nav.click();
        };
    }

    function bindRatingModal() {
        el.stars = $('star-picker').querySelectorAll('.star');
        el.stars.forEach(s => {
            const v = parseInt(s.dataset.v, 10);
            s.addEventListener('mouseenter', () => updateStars(v));
            s.addEventListener('mouseleave', () => updateStars(selectedStars));
            s.addEventListener('click', () => { selectedStars = v; updateStars(v); });
        });
        $('rating-cancel').onclick = () => $('rating-modal').classList.add('hidden');
        $('rating-submit').onclick = submitRating;
    }

    function bindDisputeModal() {
        $('dispute-cancel').onclick = () => $('dispute-modal').classList.add('hidden');
        $('dispute-submit').onclick = submitDispute;
        $('dispute-detail-close').onclick = () => $('dispute-detail-modal').classList.add('hidden');
        $('dispute-respond-submit').onclick = submitDisputeResponse;
    }

    function bindDirectionToggle() {
        document.querySelectorAll('#credit-direction-toggle .toggle-btn').forEach(btn => {
            btn.onclick = () => {
                document.querySelectorAll('#credit-direction-toggle .toggle-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                creditDirection = btn.dataset.dir;
                loadRatingList();
            };
        });
    }

    function cacheDom() {
        el.creditRing = $('credit-ring');
        el.creditScore = $('credit-score-big');
        el.creditGrade = $('credit-grade-big');
        el.creditName = $('credit-hero-name');
        el.creditStatus = $('credit-hero-status');
        el.givenCount = $('credit-given-count');
        el.receivedCount = $('credit-received-count');
        el.ratingList = $('rating-list');
        el.disputeList = $('dispute-list');
    }

    function init() {
        cacheDom();
        bindNav();
        bindRatingModal();
        bindDisputeModal();
        bindDirectionToggle();
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

    global.CreditUI = {
        openRatingModal,
        openDisputeModal,
        openDisputeDetail,
        decorateHallCard,
        addLowCreditBanner,
        decorateMyOrderFooter,
        loadCreditMap,
        invalidateCaches,
        loadCreditTab,
        loadDisputesTab,
        toast
    };
})(window);
