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

    // 是否属于「低信用发布」范畴（用于大厅弱提示）
    function isLowCreditPublisher(credit) {
        return credit < 60;
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

    /* ---------- 接口请求 ---------- */

    // 信用档案：当前分数、等级、近 10 条评分记录
    async function fetchCredit(username, direction) {
        const url = `/api/credit?username=${encodeURIComponent(username)}` +
            `&direction=${encodeURIComponent(direction || 'received')}`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error('fetch credit failed');
        return resp.json();
    }

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

    // 暴露到全局
    window.CreditAPI = {
        levelInfo,
        newScore,
        canAccept,
        isLowCreditPublisher,
        formatTime,
        disputeStatusMap,
        fetchCredit,
        submitRating,
        fetchDisputes,
        createDispute,
        arbitrateDispute
    };
})();
