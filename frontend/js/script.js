document.addEventListener('DOMContentLoaded', () => {
    let currentUser = JSON.parse(localStorage.getItem('user')) || null;
    let currentFilter = '';
    let myOrdersView = 'created';
    let hallFilter = CreditAPI.HALL_FILTERS.ALL;

    const elements = {
        authOverlay: document.getElementById('auth-overlay'),
        mainApp: document.getElementById('main-app'),
        loginForm: document.getElementById('login-form'),
        registerForm: document.getElementById('register-form'),
        tabLogin: document.getElementById('tab-login'),
        tabRegister: document.getElementById('tab-register'),
        displayName: document.getElementById('display-name'),
        displayMajor: document.getElementById('display-major'),
        welcomeName: document.getElementById('welcome-name'),
        logoutBtn: document.getElementById('logout-btn'),
        orderList: document.getElementById('order-list'),
        orderListLow: document.getElementById('order-list-low'),
        lowCreditSection: document.getElementById('low-credit-section'),
        hallFilterBar: document.getElementById('hall-filter-bar'),
        myOrdersList: document.getElementById('my-orders-list'),
        activeCount: document.getElementById('active-count'),
        orderForm: document.getElementById('order-form'),
        navItems: document.querySelectorAll('.nav-item'),
        filterPills: document.querySelectorAll('.pill'),
        viewSections: document.querySelectorAll('.view-section'),
        showCreated: document.getElementById('show-created'),
        showAccepted: document.getElementById('show-accepted'),
        profileRealName: document.getElementById('profile-realname'),
        profileMajor: document.getElementById('profile-major-disp'),
        toast: document.getElementById('toast'),
        editProfileBtn: document.getElementById('edit-profile-btn'),
        accountSecBtn: document.getElementById('account-sec-btn'),
        profileModal: document.getElementById('profile-modal'),
        securityModal: document.getElementById('security-modal'),
        profileForm: document.getElementById('profile-form'),
        securityForm: document.getElementById('security-form'),
        editRealname: document.getElementById('edit-realname'),
        editMajor: document.getElementById('edit-major'),
        editPassword: document.getElementById('edit-password'),
        lowCreditBanner: document.getElementById('low-credit-banner')
    };

    CreditUI.setCallbacks({
        onCreditRefresh: () => {
            if (currentUser) loadProfile();
        },
        onOrdersRefresh: () => {
            fetchMyOrders();
        }
    });

    CreditUI.initModalClose();

    function showToast(msg, type) {
        if (window.CreditUI) {
            CreditUI.showToast(msg, type);
        } else {
            const t = elements.toast;
            t.textContent = msg;
            t.classList.remove('hidden');
            setTimeout(() => t.classList.add('hidden'), 3000);
        }
    }

    function updateUIForLogin() {
        if (currentUser) {
            elements.authOverlay.classList.add('hidden');
            elements.mainApp.classList.remove('hidden');
            elements.displayName.textContent = currentUser.realName;
            elements.displayMajor.textContent = currentUser.major;
            elements.welcomeName.textContent = currentUser.realName;
            const pkgCustomerDisp = document.getElementById('pkg-customer-disp');
            if (pkgCustomerDisp) pkgCustomerDisp.textContent = currentUser.realName;
            document.querySelector('[data-tab="dashboard"]').click();
            startPeriodicCheck();
        } else {
            elements.authOverlay.classList.remove('hidden');
            elements.mainApp.classList.add('hidden');
        }
    }

    elements.tabLogin.onclick = () => {
        elements.loginForm.classList.remove('hidden');
        elements.registerForm.classList.add('hidden');
        elements.tabLogin.classList.add('active');
        elements.tabRegister.classList.remove('active');
    };

    elements.tabRegister.onclick = () => {
        elements.loginForm.classList.add('hidden');
        elements.registerForm.classList.remove('hidden');
        elements.tabLogin.classList.remove('active');
        elements.tabRegister.classList.add('active');
    };

    elements.loginForm.onsubmit = async (e) => {
        e.preventDefault();
        const username = document.getElementById('login-user').value;
        const password = document.getElementById('login-pass').value;
        try {
            const resp = await fetch('/api/login', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ username, password })
            });
            if (!resp.ok) { showToast('账号或密码错误', 'error'); return; }
            const data = await resp.json();
            if (data.status === 'success') {
                currentUser = data;
                localStorage.setItem('user', JSON.stringify(data));
                updateUIForLogin();
                showToast('登录成功！', 'success');
                elements.loginForm.reset();
            } else {
                showToast('账号或密码错误', 'error');
            }
        } catch (err) { showToast('服务器连接失败', 'error'); }
    };

    elements.registerForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            username: document.getElementById('reg-user').value,
            password: document.getElementById('reg-pass').value,
            realName: document.getElementById('reg-name').value,
            major: document.getElementById('reg-major').value
        };
        try {
            const resp = await fetch('/api/register', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            const data = await resp.json();
            if (resp.ok && data.status === 'success') {
                currentUser = data;
                localStorage.setItem('user', JSON.stringify(data));
                updateUIForLogin();
                showToast('注册成功！', 'success');
                elements.registerForm.reset();
            } else {
                showToast(data.message || '注册失败', 'error');
            }
        } catch (err) { showToast('注册失败', 'error'); }
    };

    elements.logoutBtn.onclick = () => {
        localStorage.removeItem('user');
        currentUser = null;
        elements.orderList.innerHTML = '';
        elements.myOrdersList.innerHTML = '';
        elements.loginForm.reset();
        elements.registerForm.reset();
        elements.tabLogin.click();
        updateUIForLogin();
    };

    elements.navItems.forEach(item => {
        item.onclick = (e) => {
            e.preventDefault();
            const tab = item.dataset.tab;
            elements.navItems.forEach(i => i.classList.remove('active'));
            item.classList.add('active');
            elements.viewSections.forEach(v => v.classList.add('hidden'));
            const section = document.getElementById(`${tab}-tab`);
            if (section) section.classList.remove('hidden');

            if (tab === 'dashboard') fetchOrders();
            if (tab === 'my-orders') fetchMyOrders();
            if (tab === 'profile') loadProfile();
            if (tab === 'credit' && currentUser) CreditUI.loadCreditProfile(currentUser.username);
            if (tab === 'dispute' && currentUser) CreditUI.loadDisputeCenter(currentUser.username);
            if (tab === 'post-task' && currentUser) {
                CreditUI.updatePublishWarning(currentUser.creditScore || 100);
            }
        };
    });

    if (elements.hallFilterBar) {
        function onHallFilterChange(newFilter) {
            hallFilter = newFilter;
            CreditUI.renderHallFilters(elements.hallFilterBar, hallFilter, onHallFilterChange);
            fetchOrders();
        }
        CreditUI.renderHallFilters(elements.hallFilterBar, hallFilter, onHallFilterChange);
    }

    async function fetchOrders() {
        try {
            let url = `/api/orders?category=${encodeURIComponent(currentFilter)}`;
            const resp = await fetch(url);
            const orders = await resp.json();

            const active = orders.filter(o => o.status !== 'completed' && o.status !== 'cancelled' && o.status !== 'refunded').length;
            elements.activeCount.textContent = active;

            const { normal, low } = CreditAPI.groupOrdersForHall(orders, currentUser ? currentUser.username : '');

            const filteredNormal = CreditAPI.filterOrders(normal, hallFilter);
            const filteredLow = CreditAPI.filterOrders(low, hallFilter);

            renderOrderCards(filteredNormal, elements.orderList, false);

            if (elements.orderListLow && elements.lowCreditSection) {
                const showLow = hallFilter !== CreditAPI.HALL_FILTERS.GOOD_ONLY && filteredLow.length > 0;
                elements.lowCreditSection.classList.toggle('hidden', !showLow);
                if (showLow) {
                    renderOrderCards(filteredLow, elements.orderListLow, false);
                } else {
                    elements.orderListLow.innerHTML = '';
                }
            }

            if (elements.lowCreditBanner) {
                const hasLow = low.length > 0 && hallFilter !== CreditAPI.HALL_FILTERS.GOOD_ONLY;
                elements.lowCreditBanner.classList.toggle('hidden', !hasLow);
            }
        } catch (err) { console.error(err); }
    }

    function renderOrderCards(displayOrders, container, isMyOrders) {
        container.innerHTML = '';
        if (displayOrders.length === 0) {
            container.innerHTML = '<div style="grid-column: 1/-1; text-align: center; color: #64748b; padding: 40px;">暂无订单数据</div>';
            return;
        }

        displayOrders.forEach(order => {
            const card = document.createElement('div');
            const statusClass = order.frozen ? 'frozen' : order.status;
            card.className = `order-card ${statusClass}`;

            const statusMap = {
                'pending': '待接单', 'accepted': '进行中', 'delivered': '待收货',
                'completed': '已完成', 'cancelled': '已撤回', 'refunded': '已退款'
            };

            const creatorLv = CreditAPI.getLevel(order.creatorCredit || 100);
            const isLowCredit = order.creatorCredit < 60;
            const creatorBadge = CreditUI.buildCreditBadge(creatorLv);

            let workerSection = '';
            if (order.worker) {
                const workerLv = CreditAPI.getLevel(order.workerCredit || 100);
                const workerBadge = CreditUI.buildCreditBadge(workerLv);
                workerSection = `<div class="info-row"><i class="fas fa-hands-helping"></i> <span>接单人: ${order.worker} ${workerBadge}</span></div>`;
            }

            let footer = '';

            if (order.frozen) {
                footer = `<button class="btn-outline" style="color:#ef4444;" disabled><i class="fas fa-ban"></i> 纠纷处理中</button>`;
            } else if (!isMyOrders && order.status === 'pending') {
                const myLv = currentUser.creditScore || 100;
                if (myLv < 60) {
                    footer = `<button class="btn-outline" disabled style="color:#ef4444;"><i class="fas fa-exclamation-circle"></i> 信用分不足，无法接单</button>`;
                } else {
                    footer = `<button class="btn-primary" onclick="updateStatus(${order.id}, 'accepted')">确认接单</button>`;
                }
            }

            if (isMyOrders) {
                if (myOrdersView === 'accepted') {
                    if (order.status === 'accepted') {
                        footer += `<button class="btn-primary" onclick="updateStatus(${order.id}, 'delivered')">确认送达</button>`;
                        footer += `<button class="btn-outline btn-danger" onclick="openDisputeModal(${order.id})" style="color:#ef4444;border-color:#fecaca;"><i class="fas fa-gavel"></i> 发起纠纷</button>`;
                    }
                    if (order.status === 'delivered') {
                        footer += `<button class="btn-outline" disabled style="color:#2563eb;">等待发布方确认...</button>`;
                        if (!order.workerRated && order.status === 'completed') {
                            footer += `<button class="btn-primary" onclick="openRatingModal(${order.id}, '${order.creator}', '发布方')"><i class="fas fa-star"></i> 评价</button>`;
                        }
                    }
                    if (order.status === 'completed') {
                        if (!order.workerRated) {
                            footer += `<button class="btn-primary" onclick="openRatingModal(${order.id}, '${order.creator}', '发布方')"><i class="fas fa-star"></i> 评价发布方</button>`;
                        } else {
                            footer += `<button class="btn-outline" disabled style="color:#10b981;"><i class="fas fa-check"></i> 已评价</button>`;
                        }
                    }
                }

                if (myOrdersView === 'created') {
                    if (order.status === 'pending') {
                        footer += `<button class="btn-outline" style="color: #ef4444;" onclick="updateStatus(${order.id}, 'cancelled')"><i class="fas fa-undo"></i> 撤回发布</button>`;
                    }
                    if (order.status === 'accepted') {
                        footer += `<button class="btn-outline btn-danger" onclick="openDisputeModal(${order.id})" style="color:#ef4444;border-color:#fecaca;"><i class="fas fa-gavel"></i> 发起纠纷</button>`;
                    }
                    if (order.status === 'delivered') {
                        footer += `<button class="btn-primary" style="background:var(--accent);color:#fff" onclick="updateStatus(${order.id}, 'completed')">确认收货</button>`;
                        footer += `<button class="btn-outline btn-danger" onclick="openDisputeModal(${order.id})" style="color:#ef4444;border-color:#fecaca;"><i class="fas fa-gavel"></i> 发起纠纷</button>`;
                    }
                    if (order.status === 'completed') {
                        if (!order.creatorRated) {
                            footer += `<button class="btn-primary" onclick="openRatingModal(${order.id}, '${order.worker}', '接单方')"><i class="fas fa-star"></i> 评价接单方</button>`;
                        } else {
                            footer += `<button class="btn-outline" disabled style="color:#10b981;"><i class="fas fa-check"></i> 已评价</button>`;
                        }
                    }
                }
            }

            const publisherHtml = isMyOrders
                ? `<div class="info-row publisher-info">
                        <i class="fas fa-user-circle"></i>
                        <span class="${isLowCredit ? 'low-credit-mark' : ''}">发布人: ${order.creator} ${creatorBadge}${isLowCredit ? ' <i class="fas fa-exclamation-triangle" style="color:#dc2626;"></i>' : ''}</span>
                   </div>`
                : CreditUI.buildPublisherInfoHtml(order);

            card.innerHTML = `
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <span class="badge ${order.status}">${statusMap[order.status] || order.status}</span>
                    <span style="color: #f43f5e; font-weight: 800; font-size: 1.2rem;">${order.reward}</span>
                </div>
                <div class="order-body">
                    <h3>${order.package}</h3>
                    <div class="info-row"><i class="fas fa-map-marker-alt"></i> <span>${order.pickup}</span></div>
                    <div class="info-row"><i class="fas fa-door-open"></i> <span>送至: ${order.delivery}</span></div>
                    ${publisherHtml}
                    ${workerSection}
                </div>
                <div class="order-footer">${footer}</div>
            `;
            container.appendChild(card);
        });

        if (!isMyOrders) {
            CreditUI.attachCreditPanels(container);
        }
    }

    async function fetchMyOrders() {
        try {
            let url = myOrdersView === 'created'
                ? `/api/orders?creator=${currentUser.username}`
                : `/api/orders?worker=${currentUser.username}`;
            const resp = await fetch(url);
            const orders = await resp.json();
            renderOrderCards(orders, elements.myOrdersList, true);
        } catch (err) { console.error(err); }
    }

    elements.filterPills.forEach(pill => {
        pill.onclick = () => {
            elements.filterPills.forEach(p => p.classList.remove('active'));
            pill.classList.add('active');
            let ds = pill.dataset.filter;
            currentFilter = (ds === '全部') ? '' : ds;
            fetchOrders();
        };
    });

    elements.showCreated.onclick = () => {
        elements.showCreated.classList.add('active');
        elements.showAccepted.classList.remove('active');
        myOrdersView = 'created';
        fetchMyOrders();
    };
    elements.showAccepted.onclick = () => {
        elements.showAccepted.classList.add('active');
        elements.showCreated.classList.remove('active');
        myOrdersView = 'accepted';
        fetchMyOrders();
    };

    elements.orderForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            package: document.getElementById('pkg-name').value,
            pickup: document.getElementById('pkg-pickup').value,
            delivery: document.getElementById('pkg-delivery').value,
            reward: document.getElementById('pkg-reward').value,
            creator: currentUser.username
        };
        try {
            const resp = await fetch('/api/orders', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (resp.ok) {
                showToast('发布成功！', 'success');
                elements.orderForm.reset();
                document.querySelector('[data-tab="dashboard"]').click();
            } else {
                showToast('发布失败', 'error');
            }
        } catch (err) { showToast('发布失败', 'error'); }
    };

    window.updateStatus = async (id, status) => {
        const payload = { id, status, worker: currentUser.username };
        try {
            const resp = await fetch('/api/update_status', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            const data = await resp.json();
            if (!resp.ok) {
                showToast(data.message || '操作失败，请重试', 'error');
                return;
            }

            if (status === 'accepted') showToast('接单成功，请尽快送达！', 'success');
            else if (status === 'delivered') showToast('已送达，等待发单人确认。', 'success');
            else if (status === 'completed') {
                showToast('任务完成！', 'success');
                if (currentUser) {
                    const creditData = await CreditAPI.fetchCredit(currentUser.username);
                    if (creditData.status === 'success') {
                        currentUser.creditScore = creditData.creditScore;
                        localStorage.setItem('user', JSON.stringify(currentUser));
                    }
                }
            }
            else if (status === 'cancelled') showToast('已成功撤回该订单。', 'success');

            if (document.getElementById('dashboard-tab').classList.contains('hidden')) fetchMyOrders();
            else fetchOrders();
        } catch (err) { showToast('操作失败', 'error'); }
    };

    window.openRatingModal = (orderId, targetUser, targetLabel) => {
        CreditUI.openRatingModal(orderId, targetUser, targetLabel, currentUser, () => {
            fetchMyOrders();
            if (currentUser) {
                CreditAPI.fetchCredit(currentUser.username).then(c => {
                    if (c.status === 'success') {
                        currentUser.creditScore = c.creditScore;
                        localStorage.setItem('user', JSON.stringify(currentUser));
                    }
                });
            }
        });
    };

    window.openDisputeModal = (orderId) => {
        CreditUI.openDisputeModal(orderId, currentUser, () => {
            fetchMyOrders();
            fetchOrders();
        });
    };

    async function loadProfile() {
        elements.profileRealName.textContent = currentUser.realName;
        elements.profileMajor.textContent = currentUser.major;

        try {
            const createdResp = await fetch(`/api/orders?creator=${currentUser.username}`);
            const createdOrders = await createdResp.json();
            document.getElementById('stat-created').textContent = createdOrders.length;

            const workerResp = await fetch(`/api/orders?worker=${currentUser.username}`);
            const workerOrders = await workerResp.json();
            const completedCount = workerOrders.filter(o => o.status === 'completed').length;
            document.getElementById('stat-delivered').textContent = completedCount;

            const creditResp = await CreditAPI.fetchCredit(currentUser.username);
            if (creditResp.status === 'success') {
                document.getElementById('stat-credit').textContent = creditResp.creditScore;
                currentUser.creditScore = creditResp.creditScore;
                localStorage.setItem('user', JSON.stringify(currentUser));
            } else {
                document.getElementById('stat-credit').textContent = currentUser.creditScore || 100;
            }
        } catch (err) {
            console.error('Failed to load profile stats:', err);
            document.getElementById('stat-credit').textContent = currentUser.creditScore || 100;
        }
    }

    elements.editProfileBtn.onclick = () => {
        elements.editRealname.value = currentUser.realName;
        elements.editMajor.value = currentUser.major;
        elements.profileModal.classList.remove('hidden');
    };

    elements.accountSecBtn.onclick = () => {
        elements.editPassword.value = '';
        elements.securityModal.classList.remove('hidden');
    };

    elements.profileForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            username: currentUser.username,
            realName: elements.editRealname.value,
            major: elements.editMajor.value
        };
        try {
            const resp = await fetch('/api/update_profile', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (resp.ok) {
                currentUser.realName = payload.realName;
                currentUser.major = payload.major;
                localStorage.setItem('user', JSON.stringify(currentUser));
                loadProfile();
                updateUIForLogin();
                elements.profileModal.classList.add('hidden');
                showToast('个人资料修改成功！', 'success');
            }
        } catch (err) { showToast('修改失败', 'error'); }
    };

    elements.securityForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            username: currentUser.username,
            password: elements.editPassword.value
        };
        try {
            const resp = await fetch('/api/update_profile', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (resp.ok) {
                elements.securityModal.classList.add('hidden');
                showToast('新密码修改成功，请重新登录', 'success');
                setTimeout(() => elements.logoutBtn.click(), 1500);
            }
        } catch (err) { showToast('修改失败', 'error'); }
    };

    let checkInterval = null;
    function startPeriodicCheck() {
        if (checkInterval) clearInterval(checkInterval);
        checkInterval = setInterval(async () => {
            if (!currentUser) return;
            try {
                await CreditAPI.checkDisputes();
                const activeTab = document.querySelector('.nav-item.active');
                if (activeTab) {
                    const tab = activeTab.dataset.tab;
                    if (tab === 'dashboard') fetchOrders();
                    if (tab === 'my-orders') fetchMyOrders();
                    if (tab === 'dispute') CreditUI.loadDisputeCenter(currentUser.username);
                }
            } catch (e) {}
        }, 60000);
    }

    updateUIForLogin();
});
