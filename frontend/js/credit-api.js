/*
 * credit-api.js
 * 「信用分 + 纠纷仲裁」模块 —— 接口请求 与 业务规则 / 状态计算 层。
 * 只负责与后端 REST 交互，以及和后端保持一致的规则计算，不直接操作 DOM。
 * DOM 渲染逻辑集中在 credit-ui.js。
 */
(function () {
    'use strict';

    /* ---------- 业务规则（与后端写死逻辑保持一致） ---------- */

    // 等级映射：>=90 优 / >=75 良 / >=60 中 / <60 差
    function levelInfo(credit) {
        if (credit >= 90) return { key: 'excellent', label: '优', text: '信用优秀，深受信赖' };
        if (credit >= 75) return { key: 'good', label: '良', text: '信用良好，值得托付' };
        if (credit >= 60) return { key: 'fair', label: '中', text: '信用一般，请继续保持' };
        return { key: 'poor', label: '差', text: '信用偏低，已限制接单资格' };
    }

    // 新分 = round(旧分 * 0.8 + 评分 * 20 * 0.2)
    function newScore(oldScore, ratingScore) {
        let v = Math.round(oldScore * 0.8 + ratingScore * 20 * 0.2);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        return v;
    }

    // 低于 60 不可再接单（发布仍允许）
    function canAccept(credit) {
        return credit >= 60;
    }

    // 是否属于「低信用发布」范畴（用于大厅弱提示 / 低信用专区）
    function isLowCreditPublisher(credit) {
        return credit < 60;
    }

    // 发布提醒阈值：< 75 弱警告（不拦截）
    function needsPublishWarning(credit) {
        return credit < 75;
    }

    // 发布者是否达到「优良」（等级为优或良，即 >=75）
    function isGoodPublisher(credit) {
        return credit >= 75;
    }

    /* ---------- 大厅筛选（下沉到 API/规则层，避免堆进 script.js） ---------- */
    // 将大厅订单按当前筛选拆分为 { normal:[], low:[] }
    // filter: 'all' | 'good' | 'low'
    function partitionDashboardOrders(orders, filter) {
        const low = [];
        const normal = [];
        orders.forEach(o => {
            if (isLowCreditPublisher(o.creatorCredit)) low.push(o);
            else normal.push(o);
        });
        if (filter === 'good') {
            return { normal: normal.filter(o => isGoodPublisher(o.creatorCredit)), low: [] };
        }
        if (filter === 'low') {
            return { normal: [], low: low };
        }
        // 'all'：正常任务在上，低信用专区单独分组
        return { normal: normal, low: low };
    }

    /* ---------- 工具 ---------- */

    function formatTime(ts) {
        if (!ts) return '';
        const d = new Date(ts * 1000);
        const pad = (n) => (n < 10 ? '0' + n : '' + n);
        return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ` +
            `${pad(d.getHours())}:${pad(d.getMinutes())}`;
    }

    const disputeStatusMap = {
        pending: '待裁决',
        upheld: '已成立（退款）',
        rejected: '已驳回'
    };

    // 信用事件类型元数据：图标、颜色、标题
    const eventMeta = {
        rating_received: { icon: 'fa-star', color: '#f59e0b', title: '收到评价' },
        rating_given: { icon: 'fa-pen', color: '#6366f1', title: '给出评价' },
        dispute_created: { icon: 'fa-gavel', color: '#f43f5e', title: '发起纠纷' },
        dispute_upheld: { icon: 'fa-triangle-exclamation', color: '#dc2626', title: '纠纷成立' },
        dispute_rejected: { icon: 'fa-circle-check', color: '#10b981', title: '纠纷驳回' },
        credit_change: { icon: 'fa-arrow-trend-up', color: '#8b5cf6', title: '信用分变更' }
    };

    function metaOf(type) {
        return eventMeta[type] || { icon: 'fa-circle-info', color: '#64748b', title: '信用事件' };
    }

    /* ---------- 接口请求 ---------- */

    // 信用档案：当前分数、等级、近 10 条评分记录
    async function fetchCredit(username, direction) {
        const url = `/api/credit?username=${encodeURIComponent(username)}` +
            `&direction=${encodeURIComponent(direction || 'received')}`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error('fetch credit failed');
        return resp.json();
    }

    // 信用事件时间线（按时间倒序）
    async function fetchCreditEvents(username, limit) {
        let url = `/api/credit_events?username=${encodeURIComponent(username)}`;
        if (limit) url += `&limit=${limit}`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error('fetch credit events failed');
        return resp.json();
    }

    // 发布者信用小面板数据：复用 /api/credit（含 hasActiveDispute + 收到的评价记录）。
    // 带短时缓存，避免同一发布者被多张卡片重复请求。
    const _pubCache = {};
    async function fetchPublisherSummary(username) {
        if (_pubCache[username]) return _pubCache[username];
        const data = await fetchCredit(username, 'received');
        // 仅保留面板需要的字段；近 3 条收到的评价（已脱敏）
        const summary = {
            username: data.username,
            credit: data.credit,
            level: data.level,
            levelKey: data.levelKey,
            canAccept: data.canAccept,
            hasActiveDispute: !!data.hasActiveDispute,
            recentRatings: (data.records || []).slice(0, 3)
        };
        _pubCache[username] = summary;
        return summary;
    }
    function clearPublisherCache() { for (const k in _pubCache) delete _pubCache[k]; }

    // 提交评价
    async function submitRating(payload) {
        const resp = await fetch('/api/ratings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await resp.json().catch(() => ({}));
        return { ok: resp.ok, data };
    }

    // 纠纷列表（与我相关）
    async function fetchDisputes(username) {
        const url = `/api/disputes?username=${encodeURIComponent(username)}`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error('fetch disputes failed');
        return resp.json();
    }

    // 发起纠纷
    async function createDispute(payload) {
        const resp = await fetch('/api/disputes', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await resp.json().catch(() => ({}));
        return { ok: resp.ok, data };
    }

    // 裁决 / 触发裁决（verdict: 'upheld' | 'rejected'）
    async function arbitrateDispute(disputeId, verdict) {
        const resp = await fetch('/api/dispute_arbitrate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ disputeId, verdict })
        });
        const data = await resp.json().catch(() => ({}));
        return { ok: resp.ok, data };
    }

    // 提交纠纷补充说明
    async function submitStatement(payload) {
        const resp = await fetch('/api/dispute_statement', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await resp.json().catch(() => ({}));
        return { ok: resp.ok, data };
    }

    // 角色与证据类型文案映射
    const roleLabel = { creator: '发布方', worker: '接单方' };
    const evidenceTypeLabel = {
        screenshot: '截图说明',
        chatlog: '聊天记录',
        other: '其他'
    };

    // 暴露到全局
    window.CreditAPI = {
        levelInfo,
        newScore,
        canAccept,
        isLowCreditPublisher,
        needsPublishWarning,
        isGoodPublisher,
        partitionDashboardOrders,
        formatTime,
        disputeStatusMap,
        metaOf,
        roleLabel,
        evidenceTypeLabel,
        fetchCredit,
        fetchCreditEvents,
        fetchPublisherSummary,
        clearPublisherCache,
        submitRating,
        fetchDisputes,
        createDispute,
        arbitrateDispute,
        submitStatement
    };
})();
