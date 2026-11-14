#include "icons.h"
#include <UIAutomation.h>
#include <oleauto.h>
#include <algorithm>

bool DetectTaskbarIcons(HWND taskbar, const RECT& tb, std::vector<LayoutSlot>& out) {
    out.clear();
    IUIAutomation* ua = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&ua))) || !ua)
        return false;

    IUIAutomationElement* root = nullptr;
    IUIAutomationCondition* cond = nullptr;
    IUIAutomationElementArray* arr = nullptr;

    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = UIA_ButtonControlTypeId;

    bool ok = SUCCEEDED(ua->ElementFromHandle(taskbar, &root)) && root
           && SUCCEEDED(ua->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &cond)) && cond
           && SUCCEEDED(root->FindAll(TreeScope_Descendants, cond, &arr)) && arr;

    if (ok) {
        int n = 0;
        arr->get_Length(&n);
        out.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            IUIAutomationElement* el = nullptr;
            RECT r{};
            if (SUCCEEDED(arr->GetElement(i, &el)) && el &&
                SUCCEEDED(el->get_CurrentBoundingRectangle(&r))) {
                RECT inter{};
                if (IntersectRect(&inter, &r, &tb) && r.right > r.left && r.bottom > r.top) {
                    LayoutSlot s;
                    s.l = r.left - tb.left;  s.t = r.top - tb.top;
                    s.r = r.right - tb.left; s.b = r.bottom - tb.top;
                    out.push_back(s);
                }
            }
            if (el) el->Release();
        }
    }

    if (arr) arr->Release();
    if (cond) cond->Release();
    if (root) root->Release();
    ua->Release();

    // Sort, then drop nested or overlapping elements (UIA reports child buttons too).
    std::sort(out.begin(), out.end(),
              [](const LayoutSlot& a, const LayoutSlot& b) { return a.l < b.l; });
    std::vector<LayoutSlot> kept;
    kept.reserve(out.size());
    for (const auto& s : out)
        if (kept.empty() || s.l >= kept.back().r) kept.push_back(s);
    out.swap(kept);

    return ok && !out.empty();
}
