#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// ---------------------------------------------------------------
// Guardado / lectura de progreso
// Se combina el porcentaje REAL que ya guarda el juego en el nivel
// (m_normalPercent / m_practicePercent) con el que guarda este mod.
// ---------------------------------------------------------------
static std::string progressKey(GJGameLevel* level, bool practice) {
    return fmt::format(
        "lvl_{}_{}_{}",
        level->m_levelID.value(),
        std::string(level->m_levelName),
        practice ? "p" : "n"
    );
}

static int loadProgress(GJGameLevel* level, bool practice) {
    int saved = Mod::get()->getSavedValue<int>(progressKey(level, practice), 0);
    int game = practice
        ? static_cast<int>(level->m_practicePercent)
        : static_cast<int>(level->m_normalPercent.value());
    return std::clamp(std::max(saved, game), 0, 100);
}

static void storeProgress(GJGameLevel* level, bool practice, int pct) {
    auto key = progressKey(level, practice);
    auto mod = Mod::get();
    if (pct > mod->getSavedValue<int>(key, 0)) {
        mod->setSavedValue<int>(key, std::clamp(pct, 0, 100));
    }
}

// ---------------------------------------------------------------
// Una fila: título encima + barra (assets originales del juego:
// GJ_progressBar_001.png y bigFont.fnt). Las posiciones son relativas
// a la esquina inferior izquierda de "root"; "y" es la base de la fila.
// ---------------------------------------------------------------
static constexpr float TITLE_SCALE = 0.4f;
static constexpr float TITLE_GAP = 2.f;

static void addRow(
    CCNode* root, const char* title, int pct, ccColor3B color,
    CCSize barSize, float y, float titleH
) {
    float midX = barSize.width / 2.f;
    float barMidY = y + barSize.height / 2.f;

    // Fondo de la barra: textura original, oscura y semitransparente
    auto back = CCSprite::create("GJ_progressBar_001.png");
    if (back) {
        back->setColor({0, 0, 0});
        back->setOpacity(125);
        back->setPosition({midX, barMidY});
        root->addChild(back, 1);
    }

    // Relleno: misma textura, coloreada y recortada según el porcentaje.
    // setTextureRect usa PUNTOS (igual que getContentSize).
    if (pct > 0) {
        auto fill = CCSprite::create("GJ_progressBar_001.png");
        if (fill) {
            fill->setColor(color);
            fill->setAnchorPoint({0.f, 0.5f});
            fill->setTextureRect(CCRect(0.f, 0.f, barSize.width * pct / 100.f, barSize.height));
            fill->setScaleX(0.992f);
            fill->setScaleY(0.86f);
            fill->setPosition({barSize.width * (1.f - 0.992f) / 2.f, barMidY});
            root->addChild(fill, 2);
        }
    }

    // Porcentaje centrado dentro de la barra
    if (Mod::get()->getSettingValue<bool>("show-text")) {
        auto percent = CCLabelBMFont::create(fmt::format("{}%", pct).c_str(), "bigFont.fnt");
        percent->setScale(0.45f);
        percent->setPosition({midX, barMidY});
        root->addChild(percent, 4);
    }

    // Título encima de la barra
    auto titleLabel = CCLabelBMFont::create(title, "bigFont.fnt");
    titleLabel->setScale(TITLE_SCALE);
    titleLabel->setPosition({midX, y + barSize.height + TITLE_GAP + titleH / 2.f});
    root->addChild(titleLabel, 3);
}

// Dos filas apiladas: Normal Mode (arriba) y Practice Mode (abajo)
static CCNode* buildProgressBar(int normal, int practice) {
    auto probe = CCSprite::create("GJ_progressBar_001.png");
    CCSize barSize = probe ? probe->getContentSize() : CCSize(220.f, 20.f);

    auto probeLabel = CCLabelBMFont::create("Normal Mode", "bigFont.fnt");
    float titleH = probeLabel->getContentSize().height * TITLE_SCALE;

    constexpr float rowGap = 5.f;
    float rowH = barSize.height + TITLE_GAP + titleH;

    auto root = CCNode::create();
    root->setAnchorPoint({0.5f, 0.5f});
    root->setContentSize({barSize.width, rowH * 2.f + rowGap});

    addRow(root, "Normal Mode", normal, {0, 255, 0}, barSize, rowH + rowGap, titleH);
    addRow(root, "Practice Mode", practice, {0, 255, 255}, barSize, 0.f, titleH);
    return root;
}

// ---------------------------------------------------------------
// Recoge todos los elementos "hoja" de la pantalla (botones, sprites,
// textos) sin depender de IDs. No entra dentro de botones/sprites/textos.
// ---------------------------------------------------------------
static void collectLeaves(CCNode* node, std::vector<CCNode*>& out) {
    auto children = node->getChildren();
    if (!children) return;
    for (auto child : CCArrayExt<CCNode*>(children)) {
        if (!child->isVisible()) continue;
        bool leaf =
            typeinfo_cast<CCMenuItem*>(child) ||
            typeinfo_cast<CCSprite*>(child) ||
            typeinfo_cast<CCLabelBMFont*>(child) ||
            typeinfo_cast<CCLabelTTF*>(child);
        if (leaf) out.push_back(child);
        else collectLeaves(child, out);
    }
}

// Posición del centro del elemento en coordenadas de la capa
static CCPoint centerInLayer(CCNode* layer, CCNode* n) {
    auto parent = n->getParent();
    auto box = n->boundingBox();
    return layer->convertToNodeSpace(
        parent->convertToWorldSpace(CCPoint(box.getMidX(), box.getMidY()))
    );
}

// Mueve un elemento "dy" unidades en coordenadas de la capa
static void moveInLayer(CCNode* layer, CCNode* n, float dy) {
    auto parent = n->getParent();
    auto wp = parent->convertToWorldSpace(n->getPosition());
    auto lp = layer->convertToNodeSpace(wp);
    auto target = parent->convertToNodeSpace(layer->convertToWorldSpace(lp + CCPoint(0.f, dy)));
    n->setPosition(target);
}

// ---------------------------------------------------------------
// Pantalla del nivel (editar / jugar / compartir)
// ---------------------------------------------------------------
class $modify(ProgressEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto winSize = CCDirector::get()->getWinSize();
        float cx = winSize.width / 2.f;

        float buttonsShift = -winSize.height * 0.10f; // botones grandes: bajan 10%
        float infoShift = -winSize.height * 0.06f;    // fila reloj / música / Unverified: baja 6%

        std::vector<CCNode*> leaves;
        collectLeaves(this, leaves);

        // Primero se calculan las posiciones, luego se mueve (para no alterar el criterio)
        std::vector<CCNode*> bigButtons;
        std::vector<CCNode*> infoRow;
        for (auto n : leaves) {
            if (!n->getParent()) continue;
            auto c = centerInLayer(this, n);
            float xr = c.x / winSize.width;
            float yr = c.y / winSize.height;

            // Botones grandes: parte central, altura media
            if (typeinfo_cast<CCMenuItem*>(n) &&
                std::abs(c.x - cx) < winSize.width * 0.25f &&
                yr > 0.30f && yr < 0.58f) {
                bigButtons.push_back(n);
            }
            // Fila de información: reloj + "Long", música, info + "Unverified / Req. Coins"
            else if (xr > 0.20f && xr < 0.85f && yr > 0.10f && yr < 0.27f) {
                infoRow.push_back(n);
            }
        }

        float buttonsTop = winSize.height * 0.40f; // valor por defecto si no se encuentran
        bool any = false;
        float topMost = 0.f;
        for (auto n : bigButtons) {
            moveInLayer(this, n, buttonsShift);
            auto box = n->boundingBox();
            auto top = this->convertToNodeSpace(
                n->getParent()->convertToWorldSpace(CCPoint(box.getMidX(), box.getMaxY()))
            );
            topMost = any ? std::max(topMost, top.y) : top.y;
            any = true;
        }
        if (any) buttonsTop = topMost;

        for (auto n : infoRow) moveInLayer(this, n, infoShift);

        // Barras: se calculan para el hueco disponible
        auto bar = buildProgressBar(loadProgress(level, false), loadProgress(level, true));
        bar->setID("progress-bar"_spr);

        float topLimit = winSize.height * 0.60f;   // justo debajo del cuadro de descripción
        float bottomLimit = buttonsTop + 3.f;      // justo encima de los botones
        float available = std::max(topLimit - bottomLimit, 10.f);

        // Escala base: la que cabe exactamente en el hueco (esto NO cambia con el ajuste)
        float nativeH = std::max(bar->getContentSize().height, 1.f);
        float baseScale = std::min(1.0f, available / nativeH);

        // "Tamaño de la barra" = zoom: agranda/achica desde el centro del hueco,
        // sin mover nada más ni cambiar el espacio reservado.
        float zoom = static_cast<float>(Mod::get()->getSettingValue<double>("bar-scale"));
        bar->setScale(baseScale * zoom);

        bar->setPosition({cx, (topLimit + bottomLimit) / 2.f});
        this->addChild(bar, 10);

        return true;
    }
};

// ---------------------------------------------------------------
// Al jugar el nivel: guardar el mejor porcentaje (normal / práctica)
// ---------------------------------------------------------------
class $modify(ProgressPlayLayer, PlayLayer) {
    struct Fields {
        int m_lastPct = -1;
        bool m_lastPractice = false;
    };

    bool isLocalLevel() {
        return m_level && m_level->m_levelType == GJLevelType::Editor;
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        if (!this->isLocalLevel()) return;

        int pct = std::clamp(static_cast<int>(this->getCurrentPercent()), 0, 100);
        bool practice = m_isPracticeMode;

        if (pct != m_fields->m_lastPct || practice != m_fields->m_lastPractice) {
            m_fields->m_lastPct = pct;
            m_fields->m_lastPractice = practice;
            storeProgress(m_level, practice, pct);
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (!this->isLocalLevel()) return;
        storeProgress(m_level, m_isPracticeMode, 100);
        (void) Mod::get()->saveData();
    }

    void onQuit() {
        PlayLayer::onQuit();
        if (this->isLocalLevel()) (void) Mod::get()->saveData();
    }
};
